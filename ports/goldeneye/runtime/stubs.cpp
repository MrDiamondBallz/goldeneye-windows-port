#include <cstdio>
#include <cstdint>

#include "recomp.h"
#include "funcs.h"

extern "C" {

static void replaced_runtime_stub(const char* name, uint8_t*, recomp_context* ctx) {
    std::fprintf(stderr, "runtime replacement stub called: %s\n", name);
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void sizepropdef(uint8_t* rdram, recomp_context* ctx) {
    replaced_runtime_stub("sizepropdef", rdram, ctx);
}

void boot(uint8_t* rdram, recomp_context* ctx) {
    // Native bridge for the original TLB setup shim at 0x80000450/0x70000450.
    // The real MIPS body only installs a low-address TLB mapping then jumps to
    // init(0x70000510). The host harness already supplies the low-address mirror,
    // so jump straight into generated init and let the guarded probe report the
    // next real runtime blocker.
    init(rdram, ctx);
}

void resolve_TLBaddress_for_InvalidHit(uint8_t* rdram, recomp_context* ctx) {
    replaced_runtime_stub("resolve_TLBaddress_for_InvalidHit", rdram, ctx);
}

void initTLBPrepareContext(uint8_t* rdram, recomp_context* ctx) {
    replaced_runtime_stub("initTLBPrepareContext", rdram, ctx);
}

void eqpower(uint8_t* rdram, recomp_context* ctx) {
    replaced_runtime_stub("eqpower", rdram, ctx);
}

} // extern "C"
