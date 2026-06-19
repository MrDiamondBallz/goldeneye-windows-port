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
constexpr std::size_t kBranchProbeCommandLimit = 32;
constexpr std::size_t kBranchProbePreviewCount = 8;
constexpr std::size_t kSegmentCount = 16;

enum class BranchTargetClassification {
    PlausibleDisplayList,
    PayloadOrUnknown,
    Untranslated,
};

struct BranchTargetProbe {
    BranchTargetClassification classification{BranchTargetClassification::Untranslated};
    uint32_t readable{};
    uint32_t known{};
    uint32_t unknown{};
    bool enddl_seen{};
    bool stopped_on_unreadable{};
    std::array<uint8_t, kBranchProbePreviewCount> first_opcodes{};
    std::size_t first_opcode_count{};
};

struct BranchTarget {
    uint32_t vaddr{};
    uint32_t source_addr{};
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

bool renderer_scan_payload_branches_enabled() {
    const char* value = std::getenv("GOLDENEYE_RENDERER_SCAN_PAYLOAD_BRANCHES");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
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

bool is_known_gbi_or_rdp_opcode(uint32_t opcode) {
    switch (opcode) {
    case kGbiMatrix:
    case kGbiVertex:
    case kGbiBranchDl:
    case kGbiTriangle1:
    case kGbiCullDl:
    case kGbiPopMatrix:
    case kGbiMoveWord:
    case kGbiTexture:
    case kGbiSetGeometryMode:
    case kGbiClearGeometryMode:
    case kGbiEndDisplayList:
    case 0xB9u:
    case 0xBAu:
    case 0xE6u:
    case 0xE7u:
    case 0xE8u:
    case 0xE9u:
    case 0xEFu:
    case 0xF0u:
    case 0xF2u:
    case 0xF3u:
    case 0xF4u:
    case 0xF5u:
    case 0xF6u:
    case 0xFCu:
    case 0xFDu:
    case 0xFEu:
    case 0xFFu:
        return true;
    default:
        return false;
    }
}

bool is_valid_set_image_command(uint32_t w0) {
    // gDPSet{Color,Texture}Image encodes G_IM_FMT_* in bits 21..23 and
    // G_IM_SIZ_* in bits 19..20. GoldenEye's PR/gbi.h defines legal formats
    // as RGBA/YUV/CI/IA/I (0..4). Higher values mean the scanner has walked
    // non-display-list payload bytes that merely happen to start with 0xFD.
    const uint32_t format = (w0 >> 21) & 0x7u;
    return format <= 4u;
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

bool resolve_runtime_resource_address(
    uint8_t* rdram,
    uint32_t raw_address,
    const std::array<uint32_t, kSegmentCount>& segments,
    uint32_t* out_vaddr,
    bool* out_valid) {
    if (out_vaddr == nullptr || out_valid == nullptr) {
        return false;
    }

    uint32_t candidate = raw_address;
    if (is_segmented_address(raw_address)) {
        if (!resolve_segmented_address(raw_address, segments, &candidate)) {
            *out_valid = false;
            return false;
        }
    } else if (raw_address < 0x00800000u) {
        candidate = 0x80000000u + raw_address;
    }

    uint8_t* ptr = nullptr;
    *out_vaddr = candidate;
    *out_valid = goldeneye_runtime_translate(rdram, candidate, 1, &ptr);
    return true;
}

bool read_command(uint8_t* rdram, uint32_t command_addr, uint32_t* w0, uint32_t* w1) {
    return can_translate_u64_command(rdram, command_addr)
        && read_u32(rdram, command_addr, w0)
        && read_u32(rdram, command_addr + 4u, w1);
}

bool looks_like_display_list_neighborhood(uint8_t* rdram, uint32_t command_addr) {
    uint32_t readable = 0;
    uint32_t known = 0;
    bool has_texture_sequence_neighbor = false;

    for (int delta = -2; delta <= 2; ++delta) {
        const uint32_t nearby_addr = command_addr + static_cast<uint32_t>(delta * static_cast<int>(kMinimumCommandBytes));
        uint32_t nearby_w0 = 0;
        uint32_t nearby_w1 = 0;
        if (!read_command(rdram, nearby_addr, &nearby_w0, &nearby_w1)) {
            continue;
        }
        readable++;
        const uint32_t opcode = nearby_w0 >> 24;
        if (is_known_gbi_or_rdp_opcode(opcode)) {
            known++;
        }
        if (delta != 0 && (opcode == 0xE6u || opcode == 0xE7u || opcode == 0xF2u || opcode == 0xF3u || opcode == 0xF4u || opcode == 0xF5u || opcode == kGbiTexture)) {
            has_texture_sequence_neighbor = true;
        }
    }

    // A real GoldenEye display-list image command usually sits inside a dense
    // run of recognizable GBI/RDP packets. Payload bytes often only match 0xFD
    // by accident, with random-looking neighbors. Keep this heuristic simple so
    // trace output exposes uncertainty instead of hiding it.
    return readable >= 3 && (known >= 3 || (known >= 2 && has_texture_sequence_neighbor));
}

const char* branch_target_classification_name(BranchTargetClassification classification) {
    switch (classification) {
    case BranchTargetClassification::PlausibleDisplayList:
        return "plausible_display_list";
    case BranchTargetClassification::PayloadOrUnknown:
        return "payload_or_unknown";
    case BranchTargetClassification::Untranslated:
        return "untranslated";
    default:
        return "unknown";
    }
}

BranchTargetProbe probe_branch_target(uint8_t* rdram, uint32_t target) {
    BranchTargetProbe probe{};
    if (!can_translate_u64_command(rdram, target)) {
        probe.classification = BranchTargetClassification::Untranslated;
        return probe;
    }

    for (std::size_t i = 0; i < kBranchProbeCommandLimit; ++i) {
        uint32_t w0 = 0;
        uint32_t w1 = 0;
        if (!read_command(rdram, target + static_cast<uint32_t>(i * kMinimumCommandBytes), &w0, &w1)) {
            probe.stopped_on_unreadable = true;
            break;
        }

        const uint8_t opcode = static_cast<uint8_t>(w0 >> 24);
        if (probe.first_opcode_count < probe.first_opcodes.size()) {
            probe.first_opcodes[probe.first_opcode_count++] = opcode;
        }
        probe.readable++;
        if (is_known_gbi_or_rdp_opcode(opcode)) {
            probe.known++;
        } else {
            probe.unknown++;
        }
        if (opcode == kGbiEndDisplayList) {
            probe.enddl_seen = true;
            break;
        }
    }

    if (probe.readable == 0) {
        probe.classification = BranchTargetClassification::Untranslated;
        return probe;
    }

    // True branch display lists should quickly look like dense GBI/RDP command
    // streams. The false GoldenEye targets observed here start with a few
    // plausible-looking segment words, then dissolve into payload bytes and have
    // no nearby EndDL. Treat that as data by default; an env flag can still scan
    // it for forensics.
    const bool dense_known_stream = probe.known >= 6 && probe.unknown <= 2;
    const bool has_terminator_with_known_stream = probe.enddl_seen && probe.known >= 2 && probe.known >= probe.unknown;
    if (dense_known_stream || has_terminator_with_known_stream) {
        probe.classification = BranchTargetClassification::PlausibleDisplayList;
    } else {
        probe.classification = BranchTargetClassification::PayloadOrUnknown;
    }
    return probe;
}

void print_branch_target_probe(
    uint32_t source_addr,
    uint32_t target,
    uint32_t depth,
    const BranchTargetProbe& probe,
    bool scheduled) {

    std::printf(
        "host_renderer_branch_target source=0x%08X target=0x%08X depth=%u classification=%s readable=%u known=%u unknown=%u enddl=%d unreadable=%d scheduled=%d first_ops=",
        source_addr,
        target,
        depth,
        branch_target_classification_name(probe.classification),
        probe.readable,
        probe.known,
        probe.unknown,
        probe.enddl_seen ? 1 : 0,
        probe.stopped_on_unreadable ? 1 : 0,
        scheduled ? 1 : 0);
    for (std::size_t i = 0; i < probe.first_opcode_count; ++i) {
        std::printf("%s%02X", i == 0 ? "" : ",", probe.first_opcodes[i]);
    }
    if (probe.first_opcode_count == 0) {
        std::printf("none");
    }
    std::printf("\n");
}

bool texture_trace_enabled() {
    const char* value = std::getenv("GOLDENEYE_RENDERER_TRACE_TEXTURES");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void trace_texture_candidate(
    uint8_t* rdram,
    uint32_t command_addr,
    uint32_t list_start,
    uint32_t branch_source_addr,
    uint32_t depth,
    uint32_t local_command_index,
    uint32_t w0,
    uint32_t w1,
    const std::array<uint32_t, kSegmentCount>& segments,
    uint32_t resolved_address,
    bool valid,
    bool plausible_command_neighborhood,
    bool likely_payload_false_positive) {
    if (!texture_trace_enabled()) {
        return;
    }

    std::printf("host_renderer_texture_trace cmd=0x%08X list=0x%08X branch_src=0x%08X depth=%u local_index=%u w0=0x%08X w1=0x%08X resolved=0x%08X valid=%d plausible=%d payload_false_positive=%d",
        command_addr,
        list_start,
        branch_source_addr,
        depth,
        local_command_index,
        w0,
        w1,
        resolved_address,
        valid ? 1 : 0,
        plausible_command_neighborhood ? 1 : 0,
        likely_payload_false_positive ? 1 : 0);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (segments[i] != 0) {
            std::printf(" seg%zu=0x%08X", i, segments[i]);
        }
    }
    std::printf("\n");

    for (int delta = -2; delta <= 2; ++delta) {
        const uint32_t nearby_addr = command_addr + static_cast<uint32_t>(delta * static_cast<int>(kMinimumCommandBytes));
        uint32_t nearby_w0 = 0;
        uint32_t nearby_w1 = 0;
        if (read_command(rdram, nearby_addr, &nearby_w0, &nearby_w1)) {
            std::printf("  texture_trace_nearby %+d addr=0x%08X w0=0x%08X w1=0x%08X op=0x%02X\n",
                delta,
                nearby_addr,
                nearby_w0,
                nearby_w1,
                nearby_w0 >> 24);
        }
    }
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

void record_texture_image_preview(
    GoldenEyeRendererTaskResult& result,
    uint32_t command_addr,
    uint32_t list_start,
    uint32_t branch_source_addr,
    uint32_t depth,
    uint32_t local_command_index,
    uint32_t w0,
    uint32_t w1,
    uint32_t resolved_address,
    bool valid,
    bool plausible_command_neighborhood,
    bool likely_payload_false_positive,
    const std::array<uint32_t, kSegmentCount>& segments) {
    if (result.first_texture_image_count >= result.first_texture_images.size()) {
        return;
    }
    for (std::size_t i = 0; i < result.first_texture_image_count; ++i) {
        const GoldenEyeRendererTextureImagePreview& existing = result.first_texture_images[i];
        if (existing.command_addr == command_addr && existing.w0 == w0 && existing.w1 == w1) {
            return;
        }
    }

    GoldenEyeRendererTextureImagePreview& preview = result.first_texture_images[result.first_texture_image_count++];
    preview.command_addr = command_addr;
    preview.list_start = list_start;
    preview.branch_source_addr = branch_source_addr;
    preview.depth = depth;
    preview.local_command_index = local_command_index;
    preview.w0 = w0;
    preview.w1 = w1;
    preview.resolved_address = resolved_address;
    preview.segmented = is_segmented_address(w1);
    preview.valid = valid;
    preview.plausible_command_neighborhood = plausible_command_neighborhood;
    preview.likely_payload_false_positive = likely_payload_false_positive;
    if (preview.segmented) {
        preview.segment = static_cast<uint8_t>(w1 >> 24);
        preview.segment_base = segments[preview.segment];
    }
    if (valid) {
        GoldenEyeResourceProvenance resource{};
        preview.resource_backed = goldeneye_runtime_find_resource(resolved_address, 1, &resource);
        if (preview.resource_backed) {
            preview.resource_kind = goldeneye_runtime_resource_kind_name(resource.kind);
            preview.resource_bank = resource.bank;
        }
    }
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
    if (has_resolved_address) {
        GoldenEyeResourceProvenance resource{};
        packet.resource_backed = goldeneye_runtime_find_resource(resolved_address, 1, &resource);
        if (packet.resource_backed) {
            packet.resource_kind = goldeneye_runtime_resource_kind_name(resource.kind);
            packet.resource_bank = resource.bank;
            packet.resource_size = resource.size;
        }
    }
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
    uint32_t command_addr,
    uint32_t list_start,
    uint32_t branch_source_addr,
    uint32_t depth,
    uint32_t local_command_index,
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

    const auto validate_address = [&](uint32_t raw_address) {
        result.backend_address_refs++;
        if (resolve_runtime_resource_address(rdram, raw_address, segments, &resolved_address, &has_resolved_address)
            && has_resolved_address) {
            result.backend_valid_refs++;
            GoldenEyeResourceProvenance resource{};
            if (goldeneye_runtime_find_resource(resolved_address, 1, &resource)) {
                result.backend_backed_refs++;
            } else {
                result.backend_unbacked_refs++;
            }
        } else {
            result.backend_invalid_refs++;
        }
    };

    switch (opcode) {
    case kGbiMatrix:
        result.matrix_commands++;
        result.backend_geometry_packets++;
        preview_backend_packet = true;
        validate_address(w1);
        break;
    case kGbiVertex:
        result.vertex_commands++;
        result.backend_geometry_packets++;
        preview_backend_packet = true;
        validate_address(w1);
        break;
    case kGbiTexture:
        result.texture_commands++;
        result.backend_state_packets++;
        preview_backend_packet = true;
        break;
    case kGbiTriangle1:
        result.triangle_commands++;
        result.presentation_packets++;
        result.backend_geometry_packets++;
        preview_backend_packet = true;
        break;
    case kGbiSetGeometryMode:
    case kGbiClearGeometryMode:
        result.geometry_mode_commands++;
        result.backend_state_packets++;
        preview_backend_packet = true;
        break;
    case 0xE6u: // RDPLoadSync
    case 0xE7u: // RDPPipeSync
    case 0xE8u: // RDPTileSync
    case 0xE9u: // RDPFullSync
        result.sync_commands++;
        result.backend_sync_packets++;
        preview_backend_packet = true;
        break;
    case 0xEFu: // SetOtherMode
    case 0xB9u: // SetOtherMode_L in several GoldenEye display lists
    case 0xBAu: // SetOtherMode_H in several GoldenEye display lists
        result.othermode_commands++;
        result.backend_state_packets++;
        preview_backend_packet = true;
        break;
    case 0xF2u: // SetTileSize
    case 0xF5u: // SetTile
        result.tile_setup_commands++;
        result.backend_texture_packets++;
        preview_backend_packet = true;
        break;
    case 0xF0u: // LoadTLUT
    case 0xF3u: // LoadBlock
    case 0xF4u: // LoadTile
        result.texture_load_commands++;
        result.presentation_packets++;
        result.backend_texture_packets++;
        preview_backend_packet = true;
        break;
    case 0xF6u: // FillRect
        result.fill_rect_commands++;
        result.presentation_packets++;
        result.backend_target_packets++;
        preview_backend_packet = true;
        break;
    case 0xFCu: // SetCombine
        result.combine_mode_commands++;
        result.backend_state_packets++;
        preview_backend_packet = true;
        break;
    case 0xFDu: { // SetTextureImage
        result.texture_image_raw_candidates++;
        const bool valid_set_image = is_valid_set_image_command(w0);
        const bool plausible_neighborhood = looks_like_display_list_neighborhood(rdram, command_addr);
        const bool likely_payload_false_positive = !plausible_neighborhood;

        if (likely_payload_false_positive) {
            result.texture_image_payload_false_positives++;
            trace_texture_candidate(
                rdram,
                command_addr,
                list_start,
                branch_source_addr,
                depth,
                local_command_index,
                w0,
                w1,
                segments,
                w1,
                false,
                plausible_neighborhood,
                likely_payload_false_positive);
            record_texture_image_preview(
                result,
                command_addr,
                list_start,
                branch_source_addr,
                depth,
                local_command_index,
                w0,
                w1,
                w1,
                false,
                plausible_neighborhood,
                likely_payload_false_positive,
                segments);
            break;
        }

        if (!valid_set_image) {
            result.malformed_texture_image_commands++;
            result.texture_image_malformed_dl_commands++;
            trace_texture_candidate(
                rdram,
                command_addr,
                list_start,
                branch_source_addr,
                depth,
                local_command_index,
                w0,
                w1,
                segments,
                w1,
                false,
                plausible_neighborhood,
                likely_payload_false_positive);
            record_texture_image_preview(
                result,
                command_addr,
                list_start,
                branch_source_addr,
                depth,
                local_command_index,
                w0,
                w1,
                w1,
                false,
                plausible_neighborhood,
                likely_payload_false_positive,
                segments);
            break;
        }

        result.texture_image_commands++;
        result.texture_image_real_dl_commands++;
        result.backend_texture_packets++;
        preview_backend_packet = true;
        if (is_segmented_address(w1)) {
            result.texture_image_segmented_refs++;
        }
        validate_address(w1);
        trace_texture_candidate(
            rdram,
            command_addr,
            list_start,
            branch_source_addr,
            depth,
            local_command_index,
            w0,
            w1,
            segments,
            resolved_address,
            has_resolved_address,
            plausible_neighborhood,
            likely_payload_false_positive);
        GoldenEyeResourceProvenance texture_resource{};
        const bool resource_backed = has_resolved_address && goldeneye_runtime_find_resource(resolved_address, 1, &texture_resource);
        if (has_resolved_address) {
            result.resolved_texture_image_refs++;
        } else {
            result.unresolved_texture_image_refs++;
        }
        if (resource_backed) {
            result.texture_image_real_backed++;
        } else {
            result.texture_image_real_unbacked++;
        }
        record_texture_image_preview(
            result,
            command_addr,
            list_start,
            branch_source_addr,
            depth,
            local_command_index,
            w0,
            w1,
            has_resolved_address ? resolved_address : (resolved_address != 0 ? resolved_address : w1),
            has_resolved_address,
            plausible_neighborhood,
            likely_payload_false_positive,
            segments);
        if (texture_trace_enabled()) {
            std::printf("host_renderer_texture_resource cmd=0x%08X addr=0x%08X translated=%d backed=%d kind=%s bank=%u size=0x%X\n",
                command_addr,
                has_resolved_address ? resolved_address : w1,
                has_resolved_address ? 1 : 0,
                resource_backed ? 1 : 0,
                resource_backed ? goldeneye_runtime_resource_kind_name(texture_resource.kind) : "none",
                resource_backed ? texture_resource.bank : 0xFFu,
                resource_backed ? texture_resource.size : 0u);
        }
        break;
    }
    case 0xFEu: // SetDepthImage / Z image
        result.depth_image_commands++;
        result.backend_target_packets++;
        preview_backend_packet = true;
        validate_address(w1);
        break;
    case 0xFFu: // SetColorImage
        result.color_image_commands++;
        result.backend_target_packets++;
        preview_backend_packet = true;
        validate_address(w1);
        break;
    default:
        break;
    }

    if (preview_backend_packet) {
        result.backend_packets++;
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
    uint32_t branch_source_addr,
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
        classify_command(
            rdram,
            command_addr,
            list_start,
            branch_source_addr,
            depth,
            commands_in_list,
            w0,
            w1,
            segments,
            result);

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
                result.branch_targets_considered++;
                result.branch_targets_untranslated++;
                BranchTargetProbe probe{};
                probe.classification = BranchTargetClassification::Untranslated;
                print_branch_target_probe(command_addr, branch_vaddr, depth + 1, probe, false);
            } else if (stack_contains(recursion_stack, branch_vaddr)) {
                result.cycle_references++;
            } else if (depth + 1 > depth_limit) {
                result.depth_limit_hit = true;
            } else {
                result.branch_targets_considered++;
                const BranchTargetProbe probe = probe_branch_target(rdram, branch_vaddr);
                const bool payload_scan_enabled = renderer_scan_payload_branches_enabled();
                bool scheduled = false;
                switch (probe.classification) {
                case BranchTargetClassification::PlausibleDisplayList:
                    result.branch_targets_plausible++;
                    scheduled = true;
                    break;
                case BranchTargetClassification::PayloadOrUnknown:
                    result.branch_targets_payload_or_unknown++;
                    scheduled = payload_scan_enabled;
                    if (!scheduled) {
                        result.branch_targets_skipped++;
                    }
                    break;
                case BranchTargetClassification::Untranslated:
                    result.branch_targets_untranslated++;
                    result.unresolved_references++;
                    scheduled = false;
                    break;
                }
                print_branch_target_probe(command_addr, branch_vaddr, depth + 1, probe, scheduled);
                if (scheduled) {
                    result.branch_targets_scanned++;
                    branch_targets.push_back(BranchTarget{branch_vaddr, command_addr, segments});
                }
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
            target.source_addr,
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
        "host_renderer_branch_summary targets=%u plausible=%u payload_or_unknown=%u untranslated=%u skipped=%u scheduled=%u payload_scan_enabled=%d\n",
        result.branch_targets_considered,
        result.branch_targets_plausible,
        result.branch_targets_payload_or_unknown,
        result.branch_targets_untranslated,
        result.branch_targets_skipped,
        result.branch_targets_scanned,
        renderer_scan_payload_branches_enabled() ? 1 : 0);

    std::printf(
        "host_renderer_presentation matrix=%u vertex=%u texture=%u triangles=%u geom_mode=%u tex_images=%u tex_malformed=%u tex_segmented=%u tex_resolved=%u tex_unresolved=%u color_images=%u depth_images=%u tile_setup=%u texture_loads=%u combine=%u sync=%u fill_rect=%u othermode=%u packets=%u\n",
        result.matrix_commands,
        result.vertex_commands,
        result.texture_commands,
        result.triangle_commands,
        result.geometry_mode_commands,
        result.texture_image_commands,
        result.malformed_texture_image_commands,
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

    std::printf(
        "host_renderer_texture_classification raw_candidates=%u real_dl=%u malformed_dl=%u false_payload=%u backed=%u real_unbacked=%u\n",
        result.texture_image_raw_candidates,
        result.texture_image_real_dl_commands,
        result.texture_image_malformed_dl_commands,
        result.texture_image_payload_false_positives,
        result.texture_image_real_backed,
        result.texture_image_real_unbacked);

    std::printf(
        "host_renderer_backend packets=%u geometry=%u state=%u texture=%u target=%u sync=%u address_refs=%u valid_refs=%u invalid_refs=%u backed_refs=%u unbacked_refs=%u\n",
        result.backend_packets,
        result.backend_geometry_packets,
        result.backend_state_packets,
        result.backend_texture_packets,
        result.backend_target_packets,
        result.backend_sync_packets,
        result.backend_address_refs,
        result.backend_valid_refs,
        result.backend_invalid_refs,
        result.backend_backed_refs,
        result.backend_unbacked_refs);

    print_top_opcodes(result);

    for (std::size_t i = 0; i < result.backend_packet_preview_count; ++i) {
        const GoldenEyeRendererPacketPreview& packet = result.backend_packet_previews[i];
        std::printf(
            "  host_renderer_backend_packet[%zu]=%s op=0x%02X w0=0x%08X w1=0x%08X resolved=0x%08X valid=%d backed=%d kind=%s bank=%u resource_size=0x%X\n",
            i,
            backend_packet_name(packet.opcode),
            packet.opcode,
            packet.w0,
            packet.w1,
            packet.resolved_address,
            packet.has_resolved_address ? 1 : 0,
            packet.resource_backed ? 1 : 0,
            packet.resource_kind,
            packet.resource_bank,
            packet.resource_size);
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
        const GoldenEyeRendererTextureImagePreview& preview = result.first_texture_images[i];
        std::printf(
            "  host_renderer_texture_image[%zu]=cmd=0x%08X list=0x%08X branch_src=0x%08X depth=%u local_index=%u w0=0x%08X w1=0x%08X resolved=0x%08X valid=%d backed=%d kind=%s bank=%u plausible=%d payload_false_positive=%d segmented=%d segment=%u segment_base=0x%08X\n",
            i,
            preview.command_addr,
            preview.list_start,
            preview.branch_source_addr,
            preview.depth,
            preview.local_command_index,
            preview.w0,
            preview.w1,
            preview.resolved_address,
            preview.valid ? 1 : 0,
            preview.resource_backed ? 1 : 0,
            preview.resource_kind,
            preview.resource_bank,
            preview.plausible_command_neighborhood ? 1 : 0,
            preview.likely_payload_false_positive ? 1 : 0,
            preview.segmented ? 1 : 0,
            preview.segment,
            preview.segment_base);
    }
}
