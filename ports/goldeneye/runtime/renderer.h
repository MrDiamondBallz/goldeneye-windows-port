#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct GoldenEyeRendererPacketPreview {
    uint8_t opcode{};
    uint32_t w0{};
    uint32_t w1{};
    uint32_t resolved_address{};
    bool has_resolved_address{};
};

struct GoldenEyeRendererTextureImagePreview {
    uint32_t command_addr{};
    uint32_t list_start{};
    uint32_t branch_source_addr{};
    uint32_t w0{};
    uint32_t w1{};
    uint32_t resolved_address{};
    uint32_t segment_base{};
    uint32_t depth{};
    uint32_t local_command_index{};
    uint8_t segment{};
    bool segmented{};
    bool valid{};
    bool plausible_command_neighborhood{};
    bool likely_payload_false_positive{};
};

struct GoldenEyeRendererTaskResult {
    uint32_t first_gdl{};
    uint32_t end_gdl{};
    uint32_t dlist_bytes{};
    uint32_t top_level_commands{};
    uint32_t commands_scanned{};
    uint32_t display_lists_scanned{};
    uint32_t max_depth_reached{};
    uint32_t branch_display_lists{};
    uint32_t branch_targets_considered{};
    uint32_t branch_targets_plausible{};
    uint32_t branch_targets_payload_or_unknown{};
    uint32_t branch_targets_untranslated{};
    uint32_t branch_targets_skipped{};
    uint32_t branch_targets_scanned{};
    uint32_t segmented_references{};
    uint32_t resolved_segmented_references{};
    uint32_t unresolved_references{};
    uint32_t rsp_commands{};
    uint32_t rdp_commands{};
    uint32_t enddl_commands{};
    uint32_t cycle_references{};
    uint32_t branch_commands_scanned{};
    uint32_t matrix_commands{};
    uint32_t vertex_commands{};
    uint32_t texture_commands{};
    uint32_t triangle_commands{};
    uint32_t geometry_mode_commands{};
    uint32_t texture_image_commands{};
    uint32_t texture_image_raw_candidates{};
    uint32_t texture_image_real_dl_commands{};
    uint32_t texture_image_payload_false_positives{};
    uint32_t texture_image_malformed_dl_commands{};
    uint32_t texture_image_real_unbacked{};
    uint32_t texture_image_real_backed{};
    uint32_t malformed_texture_image_commands{};
    uint32_t texture_image_segmented_refs{};
    uint32_t resolved_texture_image_refs{};
    uint32_t unresolved_texture_image_refs{};
    uint32_t color_image_commands{};
    uint32_t depth_image_commands{};
    uint32_t tile_setup_commands{};
    uint32_t texture_load_commands{};
    uint32_t combine_mode_commands{};
    uint32_t sync_commands{};
    uint32_t fill_rect_commands{};
    uint32_t othermode_commands{};
    uint32_t presentation_packets{};
    uint32_t backend_packets{};
    uint32_t backend_state_packets{};
    uint32_t backend_geometry_packets{};
    uint32_t backend_texture_packets{};
    uint32_t backend_target_packets{};
    uint32_t backend_sync_packets{};
    uint32_t backend_address_refs{};
    uint32_t backend_valid_refs{};
    uint32_t backend_invalid_refs{};
    std::array<uint32_t, 256> opcode_histogram{};
    bool command_limit_hit{};
    bool list_limit_hit{};
    bool depth_limit_hit{};
    std::array<uint64_t, 4> first_commands{};
    std::size_t first_command_count{};
    std::array<uint64_t, 8> branch_first_commands{};
    std::size_t branch_first_command_count{};
    std::array<GoldenEyeRendererTextureImagePreview, 8> first_texture_images{};
    std::size_t first_texture_image_count{};
    std::array<GoldenEyeRendererPacketPreview, 16> backend_packet_previews{};
    std::size_t backend_packet_preview_count{};
};

GoldenEyeRendererTaskResult goldeneye_renderer_execute_display_list_task(
    uint8_t* rdram,
    uint32_t first_gdl,
    uint32_t end_gdl);

void goldeneye_renderer_print_task_result(const GoldenEyeRendererTaskResult& result);
