#include <cstdio>
#include <cstdlib>
#include <cstdint>

#include "recomp.h"

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
    std::fprintf(stderr, "LOOKUP_FUNC unresolved: vram=0x%08X\n", static_cast<uint32_t>(vram));
    return nullptr;
}

void recomp_syscall_handler(uint8_t*, recomp_context*, int32_t instruction_vram) {
    std::fprintf(stderr, "recomp_syscall_handler stub: instruction_vram=0x%08X\n", static_cast<uint32_t>(instruction_vram));
}

void pause_self(uint8_t*) {
    std::fprintf(stderr, "pause_self stub\n");
}

} // extern "C"
