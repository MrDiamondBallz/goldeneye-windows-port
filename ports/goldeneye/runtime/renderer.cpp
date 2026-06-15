#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "runtime.h"
#include "renderer.h"

namespace {

constexpr uint32_t kGbiBranchDl = 0x06u;
constexpr uint32_t kMinimumCommandBytes = 8u;

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

    for (uint32_t index = 0; index < scan_commands; ++index) {
        const uint32_t command_addr = first_gdl + index * kMinimumCommandBytes;
        if (!can_translate_u64_command(rdram, command_addr)) {
            result.unresolved_references++;
            break;
        }

        uint32_t w0 = 0;
        uint32_t w1 = 0;
        if (!read_u32(rdram, command_addr, &w0) || !read_u32(rdram, command_addr + 4u, &w1)) {
            result.unresolved_references++;
            break;
        }

        if (result.first_command_count < result.first_commands.size()) {
            result.first_commands[result.first_command_count++] =
                (static_cast<uint64_t>(w0) << 32) | static_cast<uint64_t>(w1);
        }

        const uint32_t opcode = w0 >> 24;
        if (opcode == kGbiBranchDl) {
            result.branch_display_lists++;
            if (is_segmented_address(w1)) {
                result.segmented_references++;
                result.unresolved_references++;
            } else if (!can_translate_u64_command(rdram, w1)) {
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
        "host_renderer_execute first_gdl=0x%08X end_gdl=0x%08X bytes=0x%X top_commands=%u scanned=%u branch_dl=%u segmented_refs=%u unresolved_refs=%u rdp_commands=%u limit_hit=%d\n",
        result.first_gdl,
        result.end_gdl,
        result.dlist_bytes,
        result.top_level_commands,
        result.commands_scanned,
        result.branch_display_lists,
        result.segmented_references,
        result.unresolved_references,
        result.rdp_commands,
        result.command_limit_hit ? 1 : 0);

    for (std::size_t i = 0; i < result.first_command_count; ++i) {
        std::printf("  host_renderer_dlist[%zu]=0x%08X_%08X\n",
            i,
            static_cast<uint32_t>(result.first_commands[i] >> 32),
            static_cast<uint32_t>(result.first_commands[i]));
    }
}
