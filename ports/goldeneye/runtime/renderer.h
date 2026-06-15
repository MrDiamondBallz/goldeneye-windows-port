#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct GoldenEyeRendererTaskResult {
    uint32_t first_gdl{};
    uint32_t end_gdl{};
    uint32_t dlist_bytes{};
    uint32_t top_level_commands{};
    uint32_t commands_scanned{};
    uint32_t display_lists_scanned{};
    uint32_t max_depth_reached{};
    uint32_t branch_display_lists{};
    uint32_t segmented_references{};
    uint32_t resolved_segmented_references{};
    uint32_t unresolved_references{};
    uint32_t rdp_commands{};
    uint32_t enddl_commands{};
    uint32_t cycle_references{};
    uint32_t branch_commands_scanned{};
    bool command_limit_hit{};
    bool list_limit_hit{};
    bool depth_limit_hit{};
    std::array<uint64_t, 4> first_commands{};
    std::size_t first_command_count{};
    std::array<uint64_t, 8> branch_first_commands{};
    std::size_t branch_first_command_count{};
};

GoldenEyeRendererTaskResult goldeneye_renderer_execute_display_list_task(
    uint8_t* rdram,
    uint32_t first_gdl,
    uint32_t end_gdl);

void goldeneye_renderer_print_task_result(const GoldenEyeRendererTaskResult& result);
