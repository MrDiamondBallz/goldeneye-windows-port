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
    bool copied_to_runtime_memory{};
    std::string note;
};

struct GoldenEyeRuntimeState {
    std::vector<GoldenEyeSectionLoad> sections;
    std::size_t copied_sections{};
    std::size_t skipped_sections{};
    std::size_t copied_bytes{};
    std::size_t mapped_low_sections{};
};

struct GoldenEyeRuntimeDiagnostics {
    std::size_t rom_bytes{};
    std::size_t dma_copies{};
    std::size_t dma_bytes{};
    std::size_t queues_created{};
    std::size_t messages_sent{};
    std::size_t messages_received{};
    std::size_t threads_created{};
    std::size_t threads_started{};
};

constexpr std::size_t kGoldenEyeRdramSize = 8 * 1024 * 1024;
constexpr std::size_t kGoldenEyeLowMirrorBytes = 256 * 1024 * 1024;

bool goldeneye_runtime_init(uint8_t* rdram, std::size_t rdram_size, const char* rom_path, GoldenEyeRuntimeState* out_state);
void goldeneye_runtime_print_state(const GoldenEyeRuntimeState& state);
GoldenEyeRuntimeDiagnostics goldeneye_runtime_get_diagnostics();
void goldeneye_runtime_print_diagnostics();
void goldeneye_runtime_record_queue_created();
void goldeneye_runtime_record_message_sent();
void goldeneye_runtime_record_message_received();
void goldeneye_runtime_record_thread_created();
void goldeneye_runtime_record_thread_started();

bool goldeneye_runtime_translate(uint8_t* rdram, uint32_t vaddr, std::size_t size, uint8_t** out_ptr);
bool goldeneye_runtime_copy_rom_to_vaddr(uint8_t* rdram, uint32_t rom_addr, uint32_t vaddr, std::size_t size);
std::size_t goldeneye_runtime_rom_size();

bool goldeneye_has_function_metadata(uint32_t vram);
recomp_func_t* goldeneye_lookup_function(uint32_t vram);
