#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "runtime.h"
#include "renderer.h"

namespace {

constexpr uint32_t kGbiMatrix = 0x01u;
constexpr uint32_t kGbiVertex = 0x04u;
constexpr uint32_t kGbiBranchDl = 0x06u;
constexpr uint32_t kGbiTriangle1 = 0xBFu;
constexpr uint32_t kGbiCullDl = 0xBEu;
constexpr uint32_t kGbiPopMatrix = 0xBDu;
constexpr uint32_t kGbiMoveWord = 0xBCu;
constexpr uint32_t kGbiTexture = 0xBBu;
constexpr uint32_t kGbiSetGeometryMode = 0xB7u;
constexpr uint32_t kGbiClearGeometryMode = 0xB6u;
constexpr uint32_t kGbiEndDisplayList = 0xB8u;
constexpr uint32_t kGbiMoveWordSegment = 0x06u;
constexpr uint32_t kMinimumCommandBytes = 8u;
constexpr std::size_t kSegmentCount = 16;

struct BranchTarget {
    uint32_t vaddr{};
    std::array<uint32_t, kSegmentCount> segments{};
};

uint32_t parse_env_u32(const char* name, uint32_t fallback, uint32_t min_value, uint32_t max_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || parsed < min_value || parsed > max_value) {
        return fallback;
    }
    return static_cast<uint32_t>(parsed);
}

uint32_t renderer_command_limit() {
    return parse_env_u32("GOLDENEYE_RENDERER_COMMAND_LIMIT", 8192, 16, 262144);
}

uint32_t renderer_list_command_limit() {
    return parse_env_u32("GOLDENEYE_RENDERER_LIST_COMMAND_LIMIT", 1024, 16, 65536);
}

uint32_t renderer_depth_limit() {
    return parse_env_u32("GOLDENEYE_RENDERER_DEPTH_LIMIT", 8, 1, 64);
}

bool read_u32(uint8_t* rdram, uint32_t vaddr, uint32_t* out) {
    uint8_t* ptr = nullptr;
    if (out == nullptr || !goldeneye_runtime_translate(rdram, vaddr, sizeof(uint32_t), &ptr)) {
        return false;
    }
    *out = static_cast<uint32_t>(MEM_W(0, S32(vaddr)));
    return true;
}

bool can_translate_u64_command(uint8_t* rdram, uint32_t vaddr) {
    uint8_t* ptr = nullptr;
    return goldeneye_runtime_translate(rdram, vaddr, sizeof(uint64_t), &ptr);
}

bool is_segmented_address(uint32_t value) {
    return value < 0x10000000u && (value >> 24) != 0;
}

bool is_rdp_opcode(uint32_t opcode) {
    return opcode >= 0xE4u || opcode == 0xB8u || opcode == 0xBAu || opcode == 0xBBu || opcode == 0xBCu;
}

bool is_rsp_opcode(uint32_t opcode) {
    return !is_rdp_opcode(opcode);
}

bool decode_segment_move_word(uint32_t w0, uint32_t w1, std::array<uint32_t, kSegmentCount>& segments) {
    const uint32_t opcode = w0 >> 24;
    const uint32_t index = w0 & 0xFFu;
    const uint32_t offset = (w0 >> 8) & 0xFFFFu;
    if (opcode != kGbiMoveWord || index != kGbiMoveWordSegment || (offset % 4u) != 0u) {
        return false;
    }
    const uint32_t segment = offset / 4u;
    if (segment >= segments.size()) {
        return false;
    }
    segments[segment] = w1;
    return true;
}

bool resolve_segmented_address(uint32_t segmented, const std::array<uint32_t, kSegmentCount>& segments, uint32_t* out_vaddr) {
    if (!is_segmented_address(segmented) || out_vaddr == nullptr) {
        return false;
    }
    const uint32_t segment = segmented >> 24;
    if (segment >= segments.size()) {
        return false;
    }
    const uint32_t base = segments[segment];
    if (base == 0 && segment != 0) {
        return false;
    }
    const uint32_t offset = segmented & 0x00FFFFFFu;
    const uint32_t resolved = base + offset;
    if (resolved < 0x00800000u) {
        *out_vaddr = 0x80000000u + resolved;
    } else {
        *out_vaddr = resolved;
    }
    return true;
}

bool read_command(uint8_t* rdram, uint32_t command_addr, uint32_t* w0, uint32_t* w1) {
    return can_translate_u64_command(rdram, command_addr)
        && read_u32(rdram, command_addr, w0)
        && read_u32(rdram, command_addr + 4u, w1);
}

void record_command_preview(GoldenEyeRendererTaskResult& result, uint64_t command, bool top_level) {
    if (top_level) {
        if (result.first_command_count < result.first_commands.size()) {
            result.first_commands[result.first_command_count++] = command;
        }
        return;
    }

    if (result.branch_first_command_count < result.branch_first_commands.size()) {
        result.branch_first_commands[result.branch_first_command_count++] = command;
    }
}

void record_texture_image_preview(GoldenEyeRendererTaskResult& result, uint32_t image_addr) {
    if (result.first_texture_image_count >= result.first_texture_images.size()) {
        return;
    }
    const auto begin = result.first_texture_images.begin();
    const auto end = begin + static_cast<std::ptrdiff_t>(result.first_texture_image_count);
    if (std::find(begin, end, image_addr) != end) {
        return;
    }
    result.first_texture_images[result.first_texture_image_count++] = image_addr;
}

void record_backend_packet_preview(
    GoldenEyeRendererTaskResult& result,
    uint32_t opcode,
    uint32_t w0,
    uint32_t w1,
    uint32_t resolved_address = 0,
    bool has_resolved_address = false) {
    if (result.backend_packet_preview_count >= result.backend_packet_previews.size()) {
        return;
    }
    GoldenEyeRendererPacketPreview& packet = result.backend_packet_previews[result.backend_packet_preview_count++];
    packet.opcode = static_cast<uint8_t>(opcode);
    packet.w0 = w0;
    packet.w1 = w1;
    packet.resolved_address = resolved_address;
    packet.has_resolved_address = has_resolved_address;
}

const char* backend_packet_name(uint8_t opcode) {
    switch (opcode) {
    case kGbiMatrix: return "matrix";
    case kGbiVertex: return "vertex";
    case kGbiTriangle1: return "tri1";
    case kGbiTexture: return "texture";
    case kGbiSetGeometryMode: return "set_geom";
    case kGbiClearGeometryMode: return "clear_geom";
    case 0xE6u: return "load_sync";
    case 0xE7u: return "pipe_sync";
    case 0xE8u: return "tile_sync";
    case 0xE9u: return "full_sync";
    case 0xF0u: return "load_tlut";
    case 0xF2u: return "set_tile_size";
    case 0xF3u: return "load_block";
    case 0xF4u: return "load_tile";
    case 0xF5u: return "set_tile";
    case 0xF6u: return "fill_rect";
    case 0xFCu: return "set_combine";
    case 0xFDu: return "set_texture_image";
    case 0xFEu: return "set_depth_image";
    case 0xFFu: return "set_color_image";
    case 0xB9u: return "set_othermode_l";
    case 0xBAu: return "set_othermode_h";
    default: return "unknown";
    }
}

bool stack_contains(const std::vector<uint32_t>& stack, uint32_t vaddr) {
    return std::find(stack.begin(), stack.end(), vaddr) != stack.end();
}

void classify_command(
    uint8_t* rdram,
    uint32_t w0,
    uint32_t w1,
    const std::array<uint32_t, kSegmentCount>& segments,
    GoldenEyeRendererTaskResult& result) {
    const uint32_t opcode = w0 >> 24;
    result.opcode_histogram[opcode]++;

    if (is_rdp_opcode(opcode)) {
        result.rdp_commands++;
    } else {
        result.rsp_commands++;
    }

    bool preview_backend_packet = false;
    bool has_resolved_address = false;
    uint32_t resolved_address = 0;

    switch (opcode) {
    case kGbiMatrix:
        result.matrix_commands++;
        preview_backend_packet = true;
        break;
    case kGbiVertex:
        result.vertex_commands++;
        preview_backend_packet = true;
        if (is_segmented_address(w1) && resolve_segmented_address(w1, segments, &resolved_address)) {
            has_resolved_address = true;
        }
        break;
    case kGbiTexture:
        result.texture_commands++;
        preview_backend_packet = true;
        break;
    case kGbiTriangle1:
        result.triangle_commands++;
        result.presentation_packets++;
        preview_backend_packet = true;
        break;
    case kGbiSetGeometryMode:
    case kGbiClearGeometryMode:
        result.geometry_mode_commands++;
        preview_backend_packet = true;
        break;
    case 0xE6u: // RDPLoadSync
    case 0xE7u: // RDPPipeSync
    case 0xE8u: // RDPTileSync
    case 0xE9u: // RDPFullSync
        result.sync_commands++;
        preview_backend_packet = true;
        break;
    case 0xEFu: // SetOtherMode
    case 0xB9u: // SetOtherMode_L in several GoldenEye display lists
    case 0xBAu: // SetOtherMode_H in several GoldenEye display lists
        result.othermode_commands++;
        preview_backend_packet = true;
        break;
    case 0xF2u: // SetTileSize
    case 0xF5u: // SetTile
        result.tile_setup_commands++;
        preview_backend_packet = true;
        break;
    case 0xF0u: // LoadTLUT
    case 0xF3u: // LoadBlock
    case 0xF4u: // LoadTile
        result.texture_load_commands++;
        result.presentation_packets++;
        preview_backend_packet = true;
        break;
    case 0xF6u: // FillRect
        result.fill_rect_commands++;
        result.presentation_packets++;
        preview_backend_packet = true;
        break;
    case 0xFCu: // SetCombine
        result.combine_mode_commands++;
        preview_backend_packet = true;
        break;
    case 0xFDu: // SetTextureImage
        result.texture_image_commands++;
        preview_backend_packet = true;
        if (is_segmented_address(w1)) {
            result.texture_image_segmented_refs++;
            uint32_t resolved_image = 0;
            if (resolve_segmented_address(w1, segments, &resolved_image)) {
                uint8_t* image_ptr = nullptr;
                resolved_address = resolved_image;
                has_resolved_address = goldeneye_runtime_translate(rdram, resolved_image, 1, &image_ptr);
                if (has_resolved_address) {
                    result.resolved_texture_image_refs++;
                } else {
                    result.unresolved_texture_image_refs++;
                }
                record_texture_image_preview(result, resolved_image);
            } else {
                result.unresolved_texture_image_refs++;
                record_texture_image_preview(result, w1);
            }
        } else {
            record_texture_image_preview(result, w1);
        }
        break;
    case 0xFEu: // SetDepthImage / Z image
        result.depth_image_commands++;
        preview_backend_packet = true;
        break;
    case 0xFFu: // SetColorImage
        result.color_image_commands++;
        preview_backend_packet = true;
        break;
    default:
        break;
    }

    if (preview_backend_packet) {
        record_backend_packet_preview(result, opcode, w0, w1, resolved_address, has_resolved_address);
    }
}

void print_top_opcodes(const GoldenEyeRendererTaskResult& result) {
    std::array<uint8_t, 256> opcodes{};
    for (std::size_t i = 0; i < opcodes.size(); ++i) {
        opcodes[i] = static_cast<uint8_t>(i);
    }

    std::sort(opcodes.begin(), opcodes.end(), [&result](uint8_t lhs, uint8_t rhs) {
        const uint32_t lhs_count = result.opcode_histogram[lhs];
        const uint32_t rhs_count = result.opcode_histogram[rhs];
        if (lhs_count != rhs_count) {
            return lhs_count > rhs_count;
        }
        return lhs < rhs;
    });

    std::size_t printed = 0;
    std::printf("host_renderer_opcode_histogram");
    for (uint8_t opcode : opcodes) {
        const uint32_t count = result.opcode_histogram[opcode];
        if (count == 0) {
            continue;
        }
        std::printf(" op%02X=%u", opcode, count);
        printed++;
        if (printed >= 12) {
            break;
        }
    }
    if (printed == 0) {
        std::printf(" none");
    }
    std::printf("\n");
}

void scan_display_list(
    uint8_t* rdram,
    uint32_t list_start,
    uint32_t list_end,
    uint32_t depth,
    const uint32_t depth_limit,
    const uint32_t command_limit,
    const uint32_t list_command_limit,
    std::array<uint32_t, kSegmentCount> segments,
    std::vector<uint32_t>& recursion_stack,
    GoldenEyeRendererTaskResult& result) {

    if (result.command_limit_hit || result.depth_limit_hit) {
        return;
    }
    if (depth > depth_limit) {
        result.depth_limit_hit = true;
        return;
    }
    if (!can_translate_u64_command(rdram, list_start)) {
        result.unresolved_references++;
        return;
    }
    if (stack_contains(recursion_stack, list_start)) {
        result.cycle_references++;
        return;
    }

    recursion_stack.push_back(list_start);
    result.display_lists_scanned++;
    result.max_depth_reached = std::max(result.max_depth_reached, depth);

    std::vector<BranchTarget> branch_targets;
    branch_targets.reserve(8);

    uint32_t command_addr = list_start;
    uint32_t commands_in_list = 0;
    const bool has_known_end = list_end > list_start;

    while (!result.command_limit_hit && !result.depth_limit_hit) {
        if (result.commands_scanned >= command_limit) {
            result.command_limit_hit = true;
            break;
        }
        if (commands_in_list >= list_command_limit) {
            result.list_limit_hit = true;
            break;
        }
        if (has_known_end && command_addr >= list_end) {
            break;
        }

        uint32_t w0 = 0;
        uint32_t w1 = 0;
        if (!read_command(rdram, command_addr, &w0, &w1)) {
            result.unresolved_references++;
            break;
        }

        const uint64_t command = (static_cast<uint64_t>(w0) << 32) | static_cast<uint64_t>(w1);
        record_command_preview(result, command, depth == 0 && commands_in_list < 4);

        const uint32_t opcode = w0 >> 24;
        decode_segment_move_word(w0, w1, segments);
        classify_command(rdram, w0, w1, segments, result);

        result.commands_scanned++;
        if (depth > 0) {
            result.branch_commands_scanned++;
        }
        if (opcode == kGbiEndDisplayList) {
            result.enddl_commands++;
            break;
        }

        if (opcode == kGbiBranchDl) {
            result.branch_display_lists++;
            uint32_t branch_vaddr = w1;
            bool resolved = true;
            if (is_segmented_address(w1)) {
                result.segmented_references++;
                resolved = resolve_segmented_address(w1, segments, &branch_vaddr);
                if (resolved) {
                    result.resolved_segmented_references++;
                }
            }

            if (!resolved || !can_translate_u64_command(rdram, branch_vaddr)) {
                result.unresolved_references++;
            } else if (stack_contains(recursion_stack, branch_vaddr)) {
                result.cycle_references++;
            } else if (depth + 1 > depth_limit) {
                result.depth_limit_hit = true;
            } else {
                branch_targets.push_back(BranchTarget{branch_vaddr, segments});
            }
        }

        command_addr += kMinimumCommandBytes;
        commands_in_list++;
    }

    for (const BranchTarget& target : branch_targets) {
        if (result.command_limit_hit || result.depth_limit_hit) {
            break;
        }
        scan_display_list(
            rdram,
            target.vaddr,
            0,
            depth + 1,
            depth_limit,
            command_limit,
            list_command_limit,
            target.segments,
            recursion_stack,
            result);
    }

    recursion_stack.pop_back();
}

} // namespace

GoldenEyeRendererTaskResult goldeneye_renderer_execute_display_list_task(
    uint8_t* rdram,
    uint32_t first_gdl,
    uint32_t end_gdl) {
    GoldenEyeRendererTaskResult result{};
    result.first_gdl = first_gdl;
    result.end_gdl = end_gdl;
    result.dlist_bytes = end_gdl >= first_gdl ? end_gdl - first_gdl : 0;
    result.top_level_commands = result.dlist_bytes / kMinimumCommandBytes;

    std::array<uint32_t, kSegmentCount> segments{};
    std::vector<uint32_t> recursion_stack;
    recursion_stack.reserve(renderer_depth_limit() + 1u);

    scan_display_list(
        rdram,
        first_gdl,
        end_gdl,
        0,
        renderer_depth_limit(),
        renderer_command_limit(),
        renderer_list_command_limit(),
        segments,
        recursion_stack,
        result);

    return result;
}

void goldeneye_renderer_print_task_result(const GoldenEyeRendererTaskResult& result) {
    std::printf(
        "host_renderer_execute first_gdl=0x%08X end_gdl=0x%08X bytes=0x%X top_commands=%u scanned=%u lists=%u max_depth=%u branch_dl=%u segmented_refs=%u resolved_segmented_refs=%u unresolved_refs=%u branch_scanned=%u rsp_commands=%u rdp_commands=%u enddl=%u cycles=%u limit_hit=%d list_limit_hit=%d depth_limit_hit=%d\n",
        result.first_gdl,
        result.end_gdl,
        result.dlist_bytes,
        result.top_level_commands,
        result.commands_scanned,
        result.display_lists_scanned,
        result.max_depth_reached,
        result.branch_display_lists,
        result.segmented_references,
        result.resolved_segmented_references,
        result.unresolved_references,
        result.branch_commands_scanned,
        result.rsp_commands,
        result.rdp_commands,
        result.enddl_commands,
        result.cycle_references,
        result.command_limit_hit ? 1 : 0,
        result.list_limit_hit ? 1 : 0,
        result.depth_limit_hit ? 1 : 0);

    std::printf(
        "host_renderer_presentation matrix=%u vertex=%u texture=%u triangles=%u geom_mode=%u tex_images=%u tex_segmented=%u tex_resolved=%u tex_unresolved=%u color_images=%u depth_images=%u tile_setup=%u texture_loads=%u combine=%u sync=%u fill_rect=%u othermode=%u packets=%u\n",
        result.matrix_commands,
        result.vertex_commands,
        result.texture_commands,
        result.triangle_commands,
        result.geometry_mode_commands,
        result.texture_image_commands,
        result.texture_image_segmented_refs,
        result.resolved_texture_image_refs,
        result.unresolved_texture_image_refs,
        result.color_image_commands,
        result.depth_image_commands,
        result.tile_setup_commands,
        result.texture_load_commands,
        result.combine_mode_commands,
        result.sync_commands,
        result.fill_rect_commands,
        result.othermode_commands,
        result.presentation_packets);

    print_top_opcodes(result);

    for (std::size_t i = 0; i < result.backend_packet_preview_count; ++i) {
        const GoldenEyeRendererPacketPreview& packet = result.backend_packet_previews[i];
        std::printf(
            "  host_renderer_backend_packet[%zu]=%s op=0x%02X w0=0x%08X w1=0x%08X resolved=0x%08X valid=%d\n",
            i,
            backend_packet_name(packet.opcode),
            packet.opcode,
            packet.w0,
            packet.w1,
            packet.resolved_address,
            packet.has_resolved_address ? 1 : 0);
    }

    for (std::size_t i = 0; i < result.first_command_count; ++i) {
        std::printf("  host_renderer_dlist[%zu]=0x%08X_%08X\n",
            i,
            static_cast<uint32_t>(result.first_commands[i] >> 32),
            static_cast<uint32_t>(result.first_commands[i]));
    }
    for (std::size_t i = 0; i < result.branch_first_command_count; ++i) {
        std::printf("  host_renderer_branch_dlist[%zu]=0x%08X_%08X\n",
            i,
            static_cast<uint32_t>(result.branch_first_commands[i] >> 32),
            static_cast<uint32_t>(result.branch_first_commands[i]));
    }
    for (std::size_t i = 0; i < result.first_texture_image_count; ++i) {
        std::printf("  host_renderer_texture_image[%zu]=0x%08X\n", i, result.first_texture_images[i]);
    }
}
