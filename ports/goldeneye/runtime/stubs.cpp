#include <cstdio>
#include <cstdint>

#include "recomp.h"

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
    replaced_runtime_stub("boot", rdram, ctx);
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
