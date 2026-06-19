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
    std::size_t preloaded_cdata_bytes{};
    std::size_t preloaded_csegment_bytes{};
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
    std::size_t threads_dispatched{};
    std::size_t rsp_tasks_started{};
    std::size_t rsp_done_messages_delivered{};
};

enum class GoldenEyeResourceKind : uint8_t {
    RomDma,
    CDataPreload,
    CSegmentElfRestore,
    MempAlloc,
    MempResize,
    DecompressStub,
    Unknown,
};

struct GoldenEyeResourceProvenance {
    uint32_t vaddr{};
    uint32_t size{};
    uint32_t source_rom{};
    uint8_t bank{0xFFu};
    GoldenEyeResourceKind kind{GoldenEyeResourceKind::Unknown};
};

struct GoldenEyeMempPool {
    uint32_t start{};
    uint32_t pos{};
    uint32_t end{};
    uint32_t prevpos{};
};

constexpr std::size_t kGoldenEyeRdramSize = 8 * 1024 * 1024;
constexpr std::size_t kGoldenEyeLowMirrorBytes = 256 * 1024 * 1024;
constexpr std::size_t kGoldenEyeHostAddressSpaceBytes = (std::size_t{4} * 1024 * 1024 * 1024) + kGoldenEyeRdramSize;

bool goldeneye_runtime_init(uint8_t* rdram, std::size_t rdram_size, const char* rom_path, GoldenEyeRuntimeState* out_state);
void goldeneye_runtime_print_state(const GoldenEyeRuntimeState& state);
GoldenEyeRuntimeDiagnostics goldeneye_runtime_get_diagnostics();
void goldeneye_runtime_print_diagnostics();
const char* goldeneye_runtime_resource_kind_name(GoldenEyeResourceKind kind);
void goldeneye_runtime_record_resource(GoldenEyeResourceKind kind, uint32_t vaddr, uint32_t size, uint32_t source_rom = 0, uint8_t bank = 0xFFu);
bool goldeneye_runtime_find_resource(uint32_t vaddr, std::size_t size, GoldenEyeResourceProvenance* out_resource);
void goldeneye_runtime_print_resource_summary();
bool goldeneye_runtime_read_memp_pool(uint8_t* rdram, uint8_t bank, GoldenEyeMempPool* out_pool);
bool goldeneye_runtime_write_memp_pool(uint8_t* rdram, uint8_t bank, const GoldenEyeMempPool& pool);
void goldeneye_runtime_record_queue_created();
void goldeneye_runtime_record_message_sent();
void goldeneye_runtime_record_message_received();
void goldeneye_runtime_record_thread_created();
void goldeneye_runtime_record_thread_started();
void goldeneye_runtime_record_thread_dispatched();
void goldeneye_runtime_record_rsp_task_started();
void goldeneye_runtime_record_rsp_done_message_delivered();
bool goldeneye_runtime_continue_after_rsp_task();
std::size_t goldeneye_runtime_rsp_task_limit();
bool goldeneye_runtime_should_stop_after_rsp_done();
std::size_t goldeneye_runtime_dispatch_started_threads(uint8_t* rdram, int32_t only_thread_id, std::size_t max_dispatches);
void goldeneye_runtime_print_thread_records();

bool goldeneye_runtime_translate(uint8_t* rdram, uint32_t vaddr, std::size_t size, uint8_t** out_ptr);
bool goldeneye_runtime_copy_rom_to_vaddr(uint8_t* rdram, uint32_t rom_addr, uint32_t vaddr, std::size_t size);
bool goldeneye_runtime_preload_csegment_from_elf(uint8_t* rdram);
std::size_t goldeneye_runtime_rom_size();

bool goldeneye_has_function_metadata(uint32_t vram);
recomp_func_t* goldeneye_lookup_function(uint32_t vram);
