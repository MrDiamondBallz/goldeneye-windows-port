#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "recomp.h"

struct GoldenEyeSectionLoad {
    std::size_t index{};
    uint32_t rom_addr{};
    uint32_t ram_addr{};
    uint32_t size{};
    bool copied_to_rdram{};
    std::string note;
};

struct GoldenEyeRuntimeState {
    std::vector<GoldenEyeSectionLoad> sections;
    std::size_t copied_sections{};
    std::size_t skipped_sections{};
    std::size_t copied_bytes{};
};

bool goldeneye_runtime_init(uint8_t* rdram, std::size_t rdram_size, const char* rom_path, GoldenEyeRuntimeState* out_state);
void goldeneye_runtime_print_state(const GoldenEyeRuntimeState& state);
bool goldeneye_has_function_metadata(uint32_t vram);
recomp_func_t* goldeneye_lookup_function(uint32_t vram);
