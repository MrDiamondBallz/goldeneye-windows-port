#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "recomp.h"
#include "runtime.h"

namespace {

constexpr int32_t kOsMesgNoBlock = 0;
constexpr int32_t kOsMesgBlock = 1;
constexpr int32_t kOsError = -1;
constexpr int32_t kOsOk = 0;

struct ThreadRecord {
    uint32_t thread{};
    int32_t id{};
    uint32_t entry{};
    uint32_t arg{};
    uint32_t stack{};
    int32_t priority{};
    bool started{};
};

std::vector<ThreadRecord> g_threads;
uint32_t g_vi_current_framebuffer = 0;
uint32_t g_vi_next_framebuffer = 0;
bool g_sp_task_yielded = false;

uint32_t as_u32(gpr value) {
    return static_cast<uint32_t>(value);
}

gpr sign32(uint32_t value) {
    return static_cast<gpr>(static_cast<int32_t>(value));
}

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
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()) * 46u;
}

bool can_access(uint8_t* rdram, uint32_t addr, std::size_t size) {
    uint8_t* ptr = nullptr;
    return goldeneye_runtime_translate(rdram, addr, size, &ptr);
}

int32_t read_word(uint8_t* rdram, uint32_t addr) {
    if (!can_access(rdram, addr, sizeof(int32_t))) {
        return 0;
    }
    return MEM_W(0, sign32(addr));
}

void write_word(uint8_t* rdram, uint32_t addr, int32_t value) {
    if (!can_access(rdram, addr, sizeof(int32_t))) {
        return;
    }
    MEM_W(0, sign32(addr)) = value;
}

uint32_t stack_arg(uint8_t* rdram, recomp_context* ctx, uint32_t offset) {
    return static_cast<uint32_t>(read_word(rdram, as_u32(ctx->r29) + offset));
}

void set_result(recomp_context* ctx, int32_t value) {
    if (ctx != nullptr) {
        ctx->r2 = static_cast<gpr>(value);
    }
}

uint64_t gpr_pair_to_u64(gpr hi, gpr lo) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(hi)) << 32) |
           static_cast<uint32_t>(lo);
}

void u64_to_gpr_pair(recomp_context* ctx, uint64_t value) {
    ctx->r2 = static_cast<gpr>(static_cast<uint32_t>(value >> 32));
    ctx->r3 = static_cast<gpr>(static_cast<uint32_t>(value));
}

bool queue_ready(uint8_t* rdram, uint32_t queue) {
    return queue != 0 && can_access(rdram, queue, 0x18);
}

void send_queue_message(uint8_t* rdram, uint32_t queue, uint32_t message) {
    if (!queue_ready(rdram, queue)) {
        return;
    }

    const int32_t valid = read_word(rdram, queue + 0x08);
    const int32_t first = read_word(rdram, queue + 0x0C);
    const int32_t count = read_word(rdram, queue + 0x10);
    const uint32_t msg_buf = static_cast<uint32_t>(read_word(rdram, queue + 0x14));
    if (count <= 0 || valid < 0 || valid >= count || msg_buf == 0) {
        return;
    }

    const int32_t slot = (first + valid) % count;
    write_word(rdram, msg_buf + static_cast<uint32_t>(slot * 4), static_cast<int32_t>(message));
    write_word(rdram, queue + 0x08, valid + 1);
    goldeneye_runtime_record_message_sent();
}

void generic_success(recomp_context* ctx) {
    set_result(ctx, kOsOk);
}

#define GE_STUB(name) void name(uint8_t*, recomp_context* ctx) { generic_success(ctx); }

} // namespace

extern "C" {

void osVirtualToPhysical_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = static_cast<gpr>(virtual_to_physical_u32(as_u32(ctx->r4)));
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

void osCreateMesgQueue_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t queue = as_u32(ctx->r4);
    const uint32_t msg_buf = as_u32(ctx->r5);
    const int32_t count = static_cast<int32_t>(ctx->r6);
    if (!queue_ready(rdram, queue) || count < 0) {
        set_result(ctx, kOsError);
        return;
    }

    write_word(rdram, queue + 0x00, 0);
    write_word(rdram, queue + 0x04, 0);
    write_word(rdram, queue + 0x08, 0);
    write_word(rdram, queue + 0x0C, 0);
    write_word(rdram, queue + 0x10, count);
    write_word(rdram, queue + 0x14, static_cast<int32_t>(msg_buf));
    goldeneye_runtime_record_queue_created();
    set_result(ctx, kOsOk);
}

void osSendMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t queue = as_u32(ctx->r4);
    const uint32_t message = as_u32(ctx->r5);
    (void)ctx->r6;

    if (!queue_ready(rdram, queue)) {
        set_result(ctx, kOsError);
        return;
    }

    const int32_t valid = read_word(rdram, queue + 0x08);
    const int32_t count = read_word(rdram, queue + 0x10);
    if (count <= 0 || valid >= count) {
        set_result(ctx, kOsError);
        return;
    }

    send_queue_message(rdram, queue, message);
    set_result(ctx, kOsOk);
}

void osRecvMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t queue = as_u32(ctx->r4);
    const uint32_t out_msg = as_u32(ctx->r5);
    const int32_t flags = static_cast<int32_t>(ctx->r6);

    if (!queue_ready(rdram, queue)) {
        set_result(ctx, kOsError);
        return;
    }

    int32_t valid = read_word(rdram, queue + 0x08);
    int32_t first = read_word(rdram, queue + 0x0C);
    const int32_t count = read_word(rdram, queue + 0x10);
    const uint32_t msg_buf = static_cast<uint32_t>(read_word(rdram, queue + 0x14));

    if (valid <= 0 || count <= 0 || msg_buf == 0) {
        set_result(ctx, flags == kOsMesgBlock ? kOsOk : kOsError);
        if (out_msg != 0) {
            write_word(rdram, out_msg, 0);
        }
        return;
    }

    const uint32_t message = static_cast<uint32_t>(read_word(rdram, msg_buf + static_cast<uint32_t>(first * 4)));
    if (out_msg != 0) {
        write_word(rdram, out_msg, static_cast<int32_t>(message));
    }
    first = (first + 1) % count;
    valid--;
    write_word(rdram, queue + 0x0C, first);
    write_word(rdram, queue + 0x08, valid);
    goldeneye_runtime_record_message_received();
    set_result(ctx, kOsOk);
}

void osSetEventMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t queue = as_u32(ctx->r5);
    const uint32_t message = as_u32(ctx->r6);
    if (queue != 0) {
        send_queue_message(rdram, queue, message);
    }
    set_result(ctx, kOsOk);
}

void osPiStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    const int32_t direction = static_cast<int32_t>(ctx->r6);
    const uint32_t dev_addr = as_u32(ctx->r7);
    const uint32_t vaddr = stack_arg(rdram, ctx, 0x10);
    const uint32_t nbytes = stack_arg(rdram, ctx, 0x14);
    const uint32_t queue = stack_arg(rdram, ctx, 0x18);

    bool ok = false;
    if (direction == 0 || direction == 1) {
        ok = goldeneye_runtime_copy_rom_to_vaddr(rdram, dev_addr, vaddr, nbytes);
    }
    if (ok && queue != 0) {
        send_queue_message(rdram, queue, 0);
    }
    set_result(ctx, ok ? kOsOk : kOsError);
}

void osPiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    const int32_t direction = static_cast<int32_t>(ctx->r4);
    const uint32_t dev_addr = as_u32(ctx->r5);
    const uint32_t vaddr = as_u32(ctx->r6);
    const uint32_t nbytes = as_u32(ctx->r7);
    bool ok = false;
    if (direction == 0 || direction == 1) {
        ok = goldeneye_runtime_copy_rom_to_vaddr(rdram, dev_addr, vaddr, nbytes);
    }
    set_result(ctx, ok ? kOsOk : kOsError);
}

void osPiGetStatus_recomp(uint8_t*, recomp_context* ctx) {
    set_result(ctx, 0);
}

void osCreateThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ThreadRecord record{};
    record.thread = as_u32(ctx->r4);
    record.id = static_cast<int32_t>(ctx->r5);
    record.entry = as_u32(ctx->r6);
    record.arg = as_u32(ctx->r7);
    record.stack = stack_arg(rdram, ctx, 0x10);
    record.priority = static_cast<int32_t>(stack_arg(rdram, ctx, 0x14));
    g_threads.push_back(record);
    goldeneye_runtime_record_thread_created();
    set_result(ctx, kOsOk);
}

void osStartThread_recomp(uint8_t*, recomp_context* ctx) {
    const uint32_t thread = as_u32(ctx->r4);
    for (ThreadRecord& record : g_threads) {
        if (record.thread == thread) {
            record.started = true;
            break;
        }
    }
    goldeneye_runtime_record_thread_started();
    set_result(ctx, kOsOk);
}

void osStopThread_recomp(uint8_t*, recomp_context* ctx) {
    const uint32_t thread = as_u32(ctx->r4);
    for (ThreadRecord& record : g_threads) {
        if (record.thread == thread) {
            record.started = false;
            break;
        }
    }
    set_result(ctx, kOsOk);
}

void osYieldThread_recomp(uint8_t*, recomp_context* ctx) { generic_success(ctx); }
void osSetThreadPri_recomp(uint8_t*, recomp_context* ctx) { generic_success(ctx); }

void osViSwapBuffer_recomp(uint8_t*, recomp_context* ctx) {
    g_vi_next_framebuffer = as_u32(ctx->r4);
    g_vi_current_framebuffer = g_vi_next_framebuffer;
    set_result(ctx, kOsOk);
}

void osViGetCurrentFramebuffer_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = sign32(g_vi_current_framebuffer); }
void osViGetNextFramebuffer_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = sign32(g_vi_next_framebuffer); }

void osSpTaskYield_recomp(uint8_t*, recomp_context* ctx) { g_sp_task_yielded = true; set_result(ctx, kOsOk); }
void osSpTaskYielded_recomp(uint8_t*, recomp_context* ctx) { set_result(ctx, g_sp_task_yielded ? 1 : 0); g_sp_task_yielded = false; }

void osAiGetLength_recomp(uint8_t*, recomp_context* ctx) { set_result(ctx, 0); }
void osAiSetFrequency_recomp(uint8_t*, recomp_context* ctx) { set_result(ctx, 32000); }
void osEepromProbe_recomp(uint8_t*, recomp_context* ctx) { set_result(ctx, 1); }
void osInitialize_recomp(uint8_t*, recomp_context* ctx) { generic_success(ctx); }
void osPiReadIo_recomp(uint8_t*, recomp_context* ctx) { set_result(ctx, 0); }

GE_STUB(osAiSetNextBuffer_recomp)
GE_STUB(osContGetQuery_recomp)
GE_STUB(osContGetReadData_recomp)
GE_STUB(osContInit_recomp)
GE_STUB(osContStartQuery_recomp)
GE_STUB(osContStartReadData_recomp)
GE_STUB(osCreatePiManager_recomp)
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
GE_STUB(osSetTimer_recomp)
GE_STUB(osSpTaskLoad_recomp)
GE_STUB(osSpTaskStartGo_recomp)
GE_STUB(osViBlack_recomp)
GE_STUB(osViRepeatLine_recomp)
GE_STUB(osViSetEvent_recomp)
GE_STUB(osViSetMode_recomp)
GE_STUB(osViSetSpecialFeatures_recomp)
GE_STUB(osViSetXScale_recomp)
GE_STUB(osViSetYScale_recomp)
GE_STUB(osWritebackDCacheAll_recomp)

#undef GE_STUB

} // extern "C"
