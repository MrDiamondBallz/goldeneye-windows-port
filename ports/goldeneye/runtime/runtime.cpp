#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

#include "recomp.h"
#include "funcs.h"
#include "runtime.h"

namespace {

struct LocalSectionInfo {
    uint32_t rom_addr;
    uint32_t ram_addr;
    uint32_t size;
    std::size_t index;
};

constexpr LocalSectionInfo kSections[] = {
    {0x00001000u, 0x80000400u, 0x00000050u, 3},
    {0x00001050u, 0x70000450u, 0x00020940u, 4},
    {0x00033590u, 0x70200000u, 0x000015A0u, 6},
    {0x00034B30u, 0x7F000000u, 0x000E2D50u, 7},
    {0x00C00000u, 0x80020D90u, 0x0003C550u, 22},
};

constexpr uint32_t kCdataRomStart = 0x00021990u;
constexpr uint32_t kInflateRomEnd = 0x00034B30u;
constexpr uint32_t kCsegmentRamStart = 0x80020D90u;

constexpr std::size_t kNumSections = 29;

std::vector<int32_t> g_section_address_storage;
std::vector<uint8_t> g_rom_bytes;
GoldenEyeRuntimeDiagnostics g_diag{};

bool entrypoint_dispatch_enabled() {
    const char* value = std::getenv("GOLDENEYE_TRY_ENTRYPOINT");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

uint64_t runtime_offset_for_vaddr(uint32_t vaddr) {
    if (vaddr >= 0x80000000u && vaddr < 0x80800000u) {
        return static_cast<uint64_t>(vaddr - 0x80000000u);
    }
    if (vaddr >= 0xA0000000u && vaddr < 0xA0800000u) {
        return static_cast<uint64_t>(vaddr - 0xA0000000u);
    }
    if (vaddr >= 0x70000000u && vaddr < 0x80000000u) {
        // N64Recomp's generated MEM_* macros subtract 0xFFFFFFFF80000000 from
        // uint64_t gpr values. For positive low addresses this wraps into the
        // 0xF0000000..0xFFFFFFFF host-offset range, so the host address space is
        // reserved sparsely and low sections are mirrored there.
        return static_cast<uint64_t>(static_cast<uint32_t>(vaddr - 0x80000000u));
    }
    return std::numeric_limits<uint64_t>::max();
}

bool load_rom_file(const char* rom_path) {
    std::ifstream rom(rom_path, std::ios::binary | std::ios::ate);
    if (!rom) {
        std::fprintf(stderr, "Failed to open ROM path for local-only segment load: %s\n", rom_path);
        return false;
    }

    const std::streamoff rom_size_signed = rom.tellg();
    if (rom_size_signed < 0) {
        std::fprintf(stderr, "Failed to determine ROM size: %s\n", rom_path);
        return false;
    }

    g_rom_bytes.assign(static_cast<std::size_t>(rom_size_signed), 0);
    rom.seekg(0, std::ios::beg);
    if (!g_rom_bytes.empty() && !rom.read(reinterpret_cast<char*>(g_rom_bytes.data()), static_cast<std::streamsize>(g_rom_bytes.size()))) {
        std::fprintf(stderr, "Failed to read ROM bytes: %s\n", rom_path);
        g_rom_bytes.clear();
        return false;
    }
    g_diag.rom_bytes = g_rom_bytes.size();
    return true;
}

} // namespace

extern "C" {

int32_t* section_addresses = nullptr;

void cop0_status_write(recomp_context* ctx, gpr value) {
    ctx->status_reg = static_cast<uint32_t>(value);
}

gpr cop0_status_read(recomp_context* ctx) {
    return ctx->status_reg;
}

void switch_error(const char* func, uint32_t vram, uint32_t jtbl) {
    std::fprintf(stderr, "switch_error: func=%s vram=0x%08X jtbl=0x%08X\n", func ? func : "(null)", vram, jtbl);
}

void do_break(uint32_t vram) {
    std::fprintf(stderr, "do_break: vram=0x%08X\n", vram);
}

recomp_func_t* get_function(int32_t vram) {
    return goldeneye_lookup_function(static_cast<uint32_t>(vram));
}

void recomp_syscall_handler(uint8_t*, recomp_context*, int32_t instruction_vram) {
    std::fprintf(stderr, "recomp_syscall_handler stub: instruction_vram=0x%08X\n", static_cast<uint32_t>(instruction_vram));
}

void pause_self(uint8_t*) {
    std::fprintf(stderr, "pause_self stub\n");
}

} // extern "C"

bool goldeneye_runtime_translate(uint8_t* rdram, uint32_t vaddr, std::size_t size, uint8_t** out_ptr) {
    if (rdram == nullptr || out_ptr == nullptr) {
        return false;
    }

    const uint64_t offset = runtime_offset_for_vaddr(vaddr);
    if (offset == std::numeric_limits<uint64_t>::max()) {
        return false;
    }

    if (size > kGoldenEyeHostAddressSpaceBytes || offset > kGoldenEyeHostAddressSpaceBytes - size) {
        return false;
    }

    *out_ptr = rdram + offset;
    return true;
}

bool goldeneye_runtime_copy_rom_to_vaddr(uint8_t* rdram, uint32_t rom_addr, uint32_t vaddr, std::size_t size) {
    if (rom_addr > g_rom_bytes.size() || size > g_rom_bytes.size() - rom_addr) {
        return false;
    }

    uint8_t* dest = nullptr;
    if (!goldeneye_runtime_translate(rdram, vaddr, size, &dest)) {
        return false;
    }

    if (size != 0) {
        std::memcpy(dest, g_rom_bytes.data() + rom_addr, size);
    }
    g_diag.dma_copies++;
    g_diag.dma_bytes += size;
    return true;
}

std::size_t goldeneye_runtime_rom_size() {
    return g_rom_bytes.size();
}

GoldenEyeRuntimeDiagnostics goldeneye_runtime_get_diagnostics() {
    return g_diag;
}

void goldeneye_runtime_print_diagnostics() {
    const GoldenEyeRuntimeDiagnostics diag = goldeneye_runtime_get_diagnostics();
    std::printf("runtime_primitives: rom_bytes=%zu dma_copies=%zu dma_bytes=%zu queues_created=%zu messages_sent=%zu messages_received=%zu threads_created=%zu threads_started=%zu\n",
        diag.rom_bytes,
        diag.dma_copies,
        diag.dma_bytes,
        diag.queues_created,
        diag.messages_sent,
        diag.messages_received,
        diag.threads_created,
        diag.threads_started);
}

void goldeneye_runtime_record_queue_created() { g_diag.queues_created++; }
void goldeneye_runtime_record_message_sent() { g_diag.messages_sent++; }
void goldeneye_runtime_record_message_received() { g_diag.messages_received++; }
void goldeneye_runtime_record_thread_created() { g_diag.threads_created++; }
void goldeneye_runtime_record_thread_started() { g_diag.threads_started++; }

bool goldeneye_has_function_metadata(uint32_t vram) {
    switch (vram) {
        case 0x80000400u: // recomp_entrypoint
        case 0x80000450u: // boot replacement seam
        case 0x70000510u: // init
        case 0x7020141Cu: // decompress_entry
        case 0x700004BCu: // get_csegmentSegmentStart
        case 0x7F06C46Cu: // return_null
            return true;
        default:
            return false;
    }
}

recomp_func_t* goldeneye_lookup_function(uint32_t vram) {
    switch (vram) {
        case 0x80000400u:
            return entrypoint_dispatch_enabled() ? recomp_entrypoint : nullptr;
        case 0x80000450u:
            return boot;
        case 0x70000510u:
            return init;
        case 0x7020141Cu:
            return decompress_entry;
        case 0x700004BCu:
            return get_csegmentSegmentStart;
        case 0x7F06C46Cu:
            return return_null;
        default:
            if (!goldeneye_has_function_metadata(vram)) {
                std::fprintf(stderr, "LOOKUP_FUNC unresolved by lightweight harness: vram=0x%08X\n", vram);
            }
            return nullptr;
    }
}

bool goldeneye_runtime_init(uint8_t* rdram, std::size_t rdram_size, const char* rom_path, GoldenEyeRuntimeState* out_state) {
    if (rdram == nullptr || rom_path == nullptr || out_state == nullptr || rdram_size != kGoldenEyeRdramSize) {
        return false;
    }

    *out_state = GoldenEyeRuntimeState{};
    g_diag = GoldenEyeRuntimeDiagnostics{};

    g_section_address_storage.assign(kNumSections, 0);
    for (const LocalSectionInfo& section : kSections) {
        if (section.index < g_section_address_storage.size()) {
            g_section_address_storage[section.index] = static_cast<int32_t>(section.ram_addr);
        }
    }
    section_addresses = g_section_address_storage.data();

    if (!load_rom_file(rom_path)) {
        return false;
    }

    for (const LocalSectionInfo& section : kSections) {
        GoldenEyeSectionLoad load{};
        load.index = section.index;
        load.rom_addr = section.rom_addr;
        load.ram_addr = section.ram_addr;
        load.size = section.size;

        const bool is_low_mirror = section.ram_addr >= 0x70000000u && section.ram_addr < 0x80000000u;
        if (!goldeneye_runtime_copy_rom_to_vaddr(rdram, section.rom_addr, section.ram_addr, section.size)) {
            load.copied_to_runtime_memory = false;
            load.note = "section does not fit in local ROM/runtime memory bounds";
            out_state->skipped_sections++;
            out_state->sections.push_back(std::move(load));
            continue;
        }

        load.copied_to_runtime_memory = true;
        load.note = is_low_mirror ? "mapped into low-address host mirror" : "copied to RDRAM";
        out_state->copied_sections++;
        out_state->copied_bytes += section.size;
        if (is_low_mirror) {
            out_state->mapped_low_sections++;
        }
        out_state->sections.push_back(std::move(load));
    }

    // The original boot flow expects the compressed cdata payload to be present
    // at _csegmentSegmentStart before init() copies it down to the inflate work
    // buffer. The ELF section metadata points section 22 at ROM 0x00C00000,
    // which is exactly EOF for the local 12 MiB ROM image, so seed the known USA
    // compressed range directly from the ROM bytes instead of treating that ELF
    // section as a file-backed ROM range.
    const std::size_t cdata_size = static_cast<std::size_t>(kInflateRomEnd - kCdataRomStart);
    if (goldeneye_runtime_copy_rom_to_vaddr(rdram, kCdataRomStart, kCsegmentRamStart, cdata_size)) {
        out_state->preloaded_cdata_bytes = cdata_size;
    } else {
        std::fprintf(stderr, "warning: failed to preload compressed cdata block rom=0x%08X ram=0x%08X size=0x%zX\n",
            kCdataRomStart,
            kCsegmentRamStart,
            cdata_size);
    }

    return true;
}

void goldeneye_runtime_print_state(const GoldenEyeRuntimeState& state) {
    std::printf("sections: copied=%zu skipped=%zu copied_bytes=%zu low_mapped=%zu cdata_preloaded=%zu\n",
        state.copied_sections,
        state.skipped_sections,
        state.copied_bytes,
        state.mapped_low_sections,
        state.preloaded_cdata_bytes);

    for (const GoldenEyeSectionLoad& section : state.sections) {
        std::printf("  section[%zu] rom=0x%08X ram=0x%08X size=0x%08X %s - %s\n",
            section.index,
            section.rom_addr,
            section.ram_addr,
            section.size,
            section.copied_to_runtime_memory ? "COPIED" : "SKIPPED",
            section.note.c_str());
    }
}
