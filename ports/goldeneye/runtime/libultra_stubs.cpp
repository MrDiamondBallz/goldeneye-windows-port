#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "recomp.h"

namespace {

uint32_t virtual_to_physical_u32(uint32_t addr) {
    if (addr >= 0x80000000u && addr < 0x80800000u) {
        return addr - 0x80000000u;
    }
    if (addr >= 0xA0000000u && addr < 0xA0800000u) {
        return addr - 0xA0000000u;
    }
    return addr;
}

uint64_t host_counter_ticks() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    const auto elapsed = clock::now() - start;
    // Approximate N64 count register cadence. Good enough for boot-harness
    // progress; timing-accurate scheduling comes later.
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()) * 46u;
}

void generic_success(recomp_context* ctx) {
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void generic_unsupported(const char* name, recomp_context* ctx) {
    // Keep stubs quiet by default; these are link-enabling shims, not real
    // runtime behavior yet. Return success-ish so controlled probes can advance.
    (void)name;
    generic_success(ctx);
}

uint64_t gpr_pair_to_u64(gpr hi, gpr lo) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(hi)) << 32) |
           static_cast<uint32_t>(lo);
}

void u64_to_gpr_pair(recomp_context* ctx, uint64_t value) {
    ctx->r2 = static_cast<gpr>(static_cast<uint32_t>(value >> 32));
    ctx->r3 = static_cast<gpr>(static_cast<uint32_t>(value));
}

} // namespace

extern "C" {

void osVirtualToPhysical_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = static_cast<gpr>(virtual_to_physical_u32(static_cast<uint32_t>(ctx->r4)));
}

void osGetCount_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = static_cast<gpr>(static_cast<uint32_t>(host_counter_ticks()));
}

void osGetTime_recomp(uint8_t*, recomp_context* ctx) {
    u64_to_gpr_pair(ctx, host_counter_ticks());
}

void __ll_mul_recomp(uint8_t*, recomp_context* ctx) {
    const int64_t lhs = static_cast<int64_t>(gpr_pair_to_u64(ctx->r4, ctx->r5));
    const int64_t rhs = static_cast<int64_t>(gpr_pair_to_u64(ctx->r6, ctx->r7));
    u64_to_gpr_pair(ctx, static_cast<uint64_t>(lhs * rhs));
}

void __ll_div_recomp(uint8_t*, recomp_context* ctx) {
    const int64_t lhs = static_cast<int64_t>(gpr_pair_to_u64(ctx->r4, ctx->r5));
    const int64_t rhs = static_cast<int64_t>(gpr_pair_to_u64(ctx->r6, ctx->r7));
    u64_to_gpr_pair(ctx, rhs == 0 ? 0 : static_cast<uint64_t>(lhs / rhs));
}

void __ull_div_recomp(uint8_t*, recomp_context* ctx) {
    const uint64_t lhs = gpr_pair_to_u64(ctx->r4, ctx->r5);
    const uint64_t rhs = gpr_pair_to_u64(ctx->r6, ctx->r7);
    u64_to_gpr_pair(ctx, rhs == 0 ? 0 : lhs / rhs);
}

void __ull_rem_recomp(uint8_t*, recomp_context* ctx) {
    const uint64_t lhs = gpr_pair_to_u64(ctx->r4, ctx->r5);
    const uint64_t rhs = gpr_pair_to_u64(ctx->r6, ctx->r7);
    u64_to_gpr_pair(ctx, rhs == 0 ? 0 : lhs % rhs);
}

void __ll_to_d_recomp(uint8_t*, recomp_context* ctx) {
    const int64_t value = static_cast<int64_t>(gpr_pair_to_u64(ctx->r4, ctx->r5));
    ctx->f0.d = static_cast<double>(value);
}

void __f_to_ll_recomp(uint8_t*, recomp_context* ctx) {
    const int64_t value = static_cast<int64_t>(ctx->f12.fl);
    u64_to_gpr_pair(ctx, static_cast<uint64_t>(value));
}

#define GE_STUB(name) void name(uint8_t*, recomp_context* ctx) { generic_unsupported(#name, ctx); }

GE_STUB(osAiGetLength_recomp)
GE_STUB(osAiSetFrequency_recomp)
GE_STUB(osAiSetNextBuffer_recomp)
GE_STUB(osContGetQuery_recomp)
GE_STUB(osContGetReadData_recomp)
GE_STUB(osContInit_recomp)
GE_STUB(osContStartQuery_recomp)
GE_STUB(osContStartReadData_recomp)
GE_STUB(osCreateMesgQueue_recomp)
GE_STUB(osCreatePiManager_recomp)
GE_STUB(osCreateThread_recomp)
GE_STUB(osCreateViManager_recomp)
GE_STUB(osDpGetCounters_recomp)
GE_STUB(osDpSetNextBuffer_recomp)
GE_STUB(osDpSetStatus_recomp)
GE_STUB(osEepromLongRead_recomp)
GE_STUB(osEepromLongWrite_recomp)
GE_STUB(osEepromRead_recomp)
GE_STUB(osEepromWrite_recomp)
GE_STUB(osInvalDCache_recomp)
GE_STUB(osInvalICache_recomp)
GE_STUB(osMotorInit_recomp)
GE_STUB(osMotorStart_recomp)
GE_STUB(osMotorStop_recomp)
GE_STUB(osPfsInit_recomp)
GE_STUB(osPiReadIo_recomp)
GE_STUB(osPiStartDma_recomp)
GE_STUB(osRecvMesg_recomp)
GE_STUB(osSendMesg_recomp)
GE_STUB(osSetEventMesg_recomp)
GE_STUB(osSetThreadPri_recomp)
GE_STUB(osSetTimer_recomp)
GE_STUB(osSpTaskLoad_recomp)
GE_STUB(osSpTaskStartGo_recomp)
GE_STUB(osSpTaskYield_recomp)
GE_STUB(osSpTaskYielded_recomp)
GE_STUB(osStartThread_recomp)
GE_STUB(osStopThread_recomp)
GE_STUB(osViBlack_recomp)
GE_STUB(osViGetCurrentFramebuffer_recomp)
GE_STUB(osViGetNextFramebuffer_recomp)
GE_STUB(osViRepeatLine_recomp)
GE_STUB(osViSetEvent_recomp)
GE_STUB(osViSetMode_recomp)
GE_STUB(osViSetSpecialFeatures_recomp)
GE_STUB(osViSetXScale_recomp)
GE_STUB(osViSetYScale_recomp)
GE_STUB(osViSwapBuffer_recomp)
GE_STUB(osWritebackDCacheAll_recomp)
GE_STUB(osYieldThread_recomp)

void osEepromProbe_recomp(uint8_t*, recomp_context* ctx) {
    // Pretend EEPROM is present for now. Real save backend comes later.
    ctx->r2 = 1;
}

void osInitialize_recomp(uint8_t*, recomp_context* ctx) {
    generic_success(ctx);
}

#undef GE_STUB

} // extern "C"
