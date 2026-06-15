#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "runtime.h"
#include "renderer.h"

namespace {

constexpr uint32_t kGbiBranchDl = 0x06u;
constexpr uint32_t kGbiMoveWord = 0xBCu;
constexpr uint32_t kGbiMoveWordSegment = 0x06u;
constexpr uint32_t kMinimumCommandBytes = 8u;
constexpr std::size_t kSegmentCount = 16;

uint32_t renderer_command_limit() {
    const char* value = std::getenv("GOLDENEYE_RENDERER_COMMAND_LIMIT");
    if (value == nullptr || value[0] == '\0') {
        return 512;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || parsed < 16 || parsed > 8192) {
        return 512;
    }
    return static_cast<uint32_t>(parsed);
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

    const uint32_t limit = renderer_command_limit();
    const uint32_t scan_commands = std::min(result.top_level_commands, limit);
    if (result.top_level_commands > limit) {
        result.command_limit_hit = true;
    }

    std::array<uint32_t, kSegmentCount> segments{};

    for (uint32_t index = 0; index < scan_commands; ++index) {
        const uint32_t command_addr = first_gdl + index * kMinimumCommandBytes;

        uint32_t w0 = 0;
        uint32_t w1 = 0;
        if (!read_command(rdram, command_addr, &w0, &w1)) {
            result.unresolved_references++;
            break;
        }

        decode_segment_move_word(w0, w1, segments);

        if (result.first_command_count < result.first_commands.size()) {
            result.first_commands[result.first_command_count++] =
                (static_cast<uint64_t>(w0) << 32) | static_cast<uint64_t>(w1);
        }

        const uint32_t opcode = w0 >> 24;
        if (opcode == kGbiBranchDl) {
            result.branch_display_lists++;
            uint32_t branch_vaddr = w1;
            if (is_segmented_address(w1)) {
                result.segmented_references++;
                if (resolve_segmented_address(w1, segments, &branch_vaddr)
                    && can_translate_u64_command(rdram, branch_vaddr)) {
                    result.resolved_segmented_references++;
                } else {
                    result.unresolved_references++;
                    result.commands_scanned++;
                    continue;
                }
            } else if (!can_translate_u64_command(rdram, w1)) {
                result.unresolved_references++;
                result.commands_scanned++;
                continue;
            }

            uint32_t branch_w0 = 0;
            uint32_t branch_w1 = 0;
            if (read_command(rdram, branch_vaddr, &branch_w0, &branch_w1)) {
                result.branch_commands_scanned++;
                if (result.branch_first_command_count < result.branch_first_commands.size()) {
                    result.branch_first_commands[result.branch_first_command_count++] =
                        (static_cast<uint64_t>(branch_w0) << 32) | static_cast<uint64_t>(branch_w1);
                }
            } else {
                result.unresolved_references++;
            }
        }

        if (is_rdp_opcode(opcode)) {
            result.rdp_commands++;
        }

        result.commands_scanned++;
    }

    return result;
}

void goldeneye_renderer_print_task_result(const GoldenEyeRendererTaskResult& result) {
    std::printf(
        "host_renderer_execute first_gdl=0x%08X end_gdl=0x%08X bytes=0x%X top_commands=%u scanned=%u branch_dl=%u segmented_refs=%u resolved_segmented_refs=%u unresolved_refs=%u branch_scanned=%u rdp_commands=%u limit_hit=%d\n",
        result.first_gdl,
        result.end_gdl,
        result.dlist_bytes,
        result.top_level_commands,
        result.commands_scanned,
        result.branch_display_lists,
        result.segmented_references,
        result.resolved_segmented_references,
        result.unresolved_references,
        result.branch_commands_scanned,
        result.rdp_commands,
        result.command_limit_hit ? 1 : 0);

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
}
