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

// Lightweight metadata copied from generated recomp_overlays.inl without pulling
// the full function table into this runtime object. Pulling that table references
// ignored/libultra replacement symbols before the runtime shim is ready.
constexpr LocalSectionInfo kSections[] = {
    {0x00001000u, 0x80000400u, 0x00000050u, 3},
    {0x00001050u, 0x70000450u, 0x00020940u, 4},
    {0x00033590u, 0x70200000u, 0x000015A0u, 6},
    {0x00034B30u, 0x7F000000u, 0x000E2D50u, 7},
    {0x00C00000u, 0x80020D90u, 0x0003C550u, 22},
};

constexpr std::size_t kNumSections = 29;

std::vector<int32_t> g_section_address_storage;

bool vram_to_rdram_offset(uint32_t vram, std::size_t rdram_size, std::size_t* out_offset) {
    uint32_t offset = 0;

    if (vram >= 0x80000000u && vram < 0x80800000u) {
        offset = vram - 0x80000000u;
    } else if (vram >= 0xA0000000u && vram < 0xA0800000u) {
        offset = vram - 0xA0000000u;
    } else {
        return false;
    }

    if (offset >= rdram_size) {
        return false;
    }

    *out_offset = offset;
    return true;
}

std::size_t safe_copy_size(uint32_t section_size, std::size_t offset, std::size_t rdram_size, std::size_t rom_addr, std::size_t rom_size) {
    std::size_t size = section_size;
    size = std::min(size, rdram_size - offset);
    if (rom_addr < rom_size) {
        size = std::min(size, rom_size - rom_addr);
    } else {
        size = 0;
    }
    return size;
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

bool goldeneye_has_function_metadata(uint32_t vram) {
    switch (vram) {
        case 0x80000400u: // recomp_entrypoint
        case 0x700004BCu: // get_csegmentSegmentStart
        case 0x7F06C46Cu: // return_null
            return true;
        default:
            return false;
    }
}

recomp_func_t* goldeneye_lookup_function(uint32_t vram) {
    // Keep this lightweight harness metadata-only for now. Returning direct
    // generated function pointers here pulls full generated translation units
    // into the host binary and exposes the next real blocker: libultra/hardware
    // replacement symbols. The boot harness proves segment load + metadata
    // resolution first; full dynamic dispatch comes after replacement coverage.
    if (!goldeneye_has_function_metadata(vram)) {
        std::fprintf(stderr, "LOOKUP_FUNC unresolved by lightweight harness: vram=0x%08X\n", vram);
    }
    return nullptr;
}

bool goldeneye_runtime_init(uint8_t* rdram, std::size_t rdram_size, const char* rom_path, GoldenEyeRuntimeState* out_state) {
    if (rdram == nullptr || rom_path == nullptr || out_state == nullptr) {
        return false;
    }

    out_state->sections.clear();
    out_state->copied_sections = 0;
    out_state->skipped_sections = 0;
    out_state->copied_bytes = 0;

    g_section_address_storage.assign(kNumSections, 0);
    for (const LocalSectionInfo& section : kSections) {
        if (section.index < g_section_address_storage.size()) {
            g_section_address_storage[section.index] = static_cast<int32_t>(section.ram_addr);
        }
    }
    section_addresses = g_section_address_storage.data();

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
    const std::size_t rom_size = static_cast<std::size_t>(rom_size_signed);

    for (const LocalSectionInfo& section : kSections) {
        GoldenEyeSectionLoad load{};
        load.index = section.index;
        load.rom_addr = section.rom_addr;
        load.ram_addr = section.ram_addr;
        load.size = section.size;

        std::size_t rdram_offset = 0;
        if (!vram_to_rdram_offset(section.ram_addr, rdram_size, &rdram_offset)) {
            load.copied_to_rdram = false;
            load.note = "not in direct KSEG0/KSEG1 RDRAM window; runtime overlay mapping still needed";
            out_state->skipped_sections++;
            out_state->sections.push_back(std::move(load));
            continue;
        }

        const std::size_t copy_size = safe_copy_size(section.size, rdram_offset, rdram_size, section.rom_addr, rom_size);
        if (copy_size == 0 || copy_size < section.size) {
            load.copied_to_rdram = false;
            load.note = "section does not fit in local ROM/RDRAM bounds";
            out_state->skipped_sections++;
            out_state->sections.push_back(std::move(load));
            continue;
        }

        rom.seekg(static_cast<std::streamoff>(section.rom_addr), std::ios::beg);
        if (!rom.read(reinterpret_cast<char*>(rdram + rdram_offset), static_cast<std::streamsize>(copy_size))) {
            load.copied_to_rdram = false;
            load.note = "ROM read failed";
            out_state->skipped_sections++;
            out_state->sections.push_back(std::move(load));
            continue;
        }

        load.copied_to_rdram = true;
        load.note = "copied to RDRAM";
        out_state->copied_sections++;
        out_state->copied_bytes += copy_size;
        out_state->sections.push_back(std::move(load));
    }

    return true;
}

void goldeneye_runtime_print_state(const GoldenEyeRuntimeState& state) {
    std::printf("sections: copied=%zu skipped=%zu copied_bytes=%zu\n",
        state.copied_sections,
        state.skipped_sections,
        state.copied_bytes);

    for (const GoldenEyeSectionLoad& section : state.sections) {
        std::printf("  section[%zu] rom=0x%08X ram=0x%08X size=0x%08X %s - %s\n",
            section.index,
            section.rom_addr,
            section.ram_addr,
            section.size,
            section.copied_to_rdram ? "COPIED" : "SKIPPED",
            section.note.c_str());
    }
}
