#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

#include "runtime.h"
#include "renderer.h"
#include "funcs.h"

extern "C" {

static void replaced_runtime_stub(const char* name, uint8_t*, recomp_context* ctx) {
    std::fprintf(stderr, "runtime replacement stub called: %s\n", name);
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

uint32_t g_host_memp_cursor = 0x80100000u;
uint32_t g_host_frame_ticks = 0;
uint32_t g_host_frame_os_count = 0;
uint32_t g_host_rsp_tasks = 0;

uint32_t align16(uint32_t value) {
    return (value + 0xFu) & ~0xFu;
}

int32_t read_runtime_word(uint8_t* rdram, uint32_t addr) {
    uint8_t* ptr = nullptr;
    if (!goldeneye_runtime_translate(rdram, addr, sizeof(int32_t), &ptr)) {
        return 0;
    }
    return MEM_W(0, S32(addr));
}

int16_t read_runtime_half(uint8_t* rdram, uint32_t addr) {
    uint8_t* ptr = nullptr;
    if (!goldeneye_runtime_translate(rdram, addr, sizeof(int16_t), &ptr)) {
        return 0;
    }
    return MEM_H(0, S32(addr));
}

void write_runtime_word(uint8_t* rdram, uint32_t addr, int32_t value) {
    uint8_t* ptr = nullptr;
    if (!goldeneye_runtime_translate(rdram, addr, sizeof(int32_t), &ptr)) {
        return;
    }
    MEM_W(0, S32(addr)) = value;
}

uint32_t frame_tick_limit() {
    const char* value = std::getenv("GOLDENEYE_FRAME_TICK_LIMIT");
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || parsed > 1000) {
        return 0;
    }
    return static_cast<uint32_t>(parsed);
}

bool enqueue_runtime_message(uint8_t* rdram, uint32_t queue, uint32_t message) {
    constexpr uint32_t kQueueValidOffset = 0x08u;
    constexpr uint32_t kQueueFirstOffset = 0x0Cu;
    constexpr uint32_t kQueueCountOffset = 0x10u;
    constexpr uint32_t kQueueMsgBufOffset = 0x14u;

    if (queue == 0) {
        return false;
    }

    const int32_t valid = read_runtime_word(rdram, queue + kQueueValidOffset);
    const int32_t first = read_runtime_word(rdram, queue + kQueueFirstOffset);
    const int32_t count = read_runtime_word(rdram, queue + kQueueCountOffset);
    const uint32_t msg_buf = static_cast<uint32_t>(read_runtime_word(rdram, queue + kQueueMsgBufOffset));
    if (count <= 0 || valid < 0 || valid >= count || msg_buf == 0) {
        return false;
    }

    const int32_t slot = (first + valid) % count;
    write_runtime_word(rdram, msg_buf + static_cast<uint32_t>(slot * 4), static_cast<int32_t>(message));
    write_runtime_word(rdram, queue + kQueueValidOffset, valid + 1);
    goldeneye_runtime_record_message_sent();
    return true;
}

void write_runtime_half(uint8_t* rdram, uint32_t addr, int16_t value) {
    uint8_t* ptr = nullptr;
    if (!goldeneye_runtime_translate(rdram, addr, sizeof(int16_t), &ptr)) {
        return;
    }
    MEM_H(0, S32(addr)) = value;
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
    // Keep the boot path moving while the generated inflate/TLB setup is still
    // skeletal by restoring the local decomp ELF's resolved csegment after init's
    // decompression step. The ELF is local-only and never committed.
    if (!goldeneye_runtime_preload_csegment_from_elf(rdram)) {
        std::fprintf(stderr, "runtime replacement warning: failed to restore local ELF csegment\n");
    }
}

void debFind(uint8_t*, recomp_context* ctx) {
    // The debug registry is nonessential for the native harness. Returning NULL
    // keeps callers on their normal "not found" path without walking generated
    // linked-list/string code while scheduler/video primitives are still stubbed.
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void debAllocate(uint8_t*, recomp_context* ctx) {
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void debAdd(uint8_t*, recomp_context* ctx) {
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void debTryAdd(uint8_t*, recomp_context* ctx) {
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void debInit(uint8_t*, recomp_context* ctx) {
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void alFxNew(uint8_t*, recomp_context* ctx) {
    // Reverb/effect construction allocates and wires a large N64 audio graph.
    // The native spike only needs audio init to be non-blocking until a host
    // audio backend owns this path.
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void mempAllocBytesInBank(uint8_t* rdram, recomp_context* ctx) {
    if (ctx == nullptr) {
        return;
    }

    const uint32_t bytes = static_cast<uint32_t>(ctx->r4);
    const uint32_t bank = static_cast<uint32_t>(ctx->r5 & 0xFFu);
    const uint32_t allocation = align16(g_host_memp_cursor);
    const uint32_t next = align16(allocation + bytes);

    uint8_t* host_ptr = nullptr;
    if (bytes == 0 || next <= allocation || next >= 0x803A0000u || !goldeneye_runtime_translate(rdram, allocation, bytes, &host_ptr)) {
        std::fprintf(stderr,
            "mempAllocBytesInBank host allocator failed bytes=0x%X bank=%u cursor=0x%08X\n",
            bytes,
            bank,
            g_host_memp_cursor);
        ctx->r2 = 0;
        return;
    }

    std::memset(host_ptr, 0, bytes);
    g_host_memp_cursor = next;
    ctx->r2 = S32(allocation);
}

void mempAddEntryOfSizeToBank(uint8_t*, recomp_context* ctx) {
    // The host allocator does not maintain the original bank-entry metadata yet.
    // Treat resize bookkeeping as successful so callers can continue through the
    // next runtime surface.
    if (ctx != nullptr) {
        ctx->r2 = 1;
    }
}

void musicSeqPlayerInit(uint8_t*, recomp_context* ctx) {
    // Audio bank loading pulls compressed asset data and builds the N64 synth
    // graph. Skip it for now; the host audio backend will replace this whole
    // path rather than relying on generated libaudio internals.
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void decompressdata(uint8_t*, recomp_context* ctx) {
    // Placeholder for compressed asset expansion. Returning 0 is enough to keep
    // early boot/main-thread probes moving to the next runtime boundary; real
    // asset streaming needs a host implementation, not generated zlib on stubbed
    // memory pools.
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void alCSeqNew(uint8_t*, recomp_context* ctx) {
    // Sequence construction currently depends on audio assets that are intentionally
    // skipped by the native spike. Treat it as a no-op until host audio exists.
    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void waitForNextFrame(uint8_t* rdram, recomp_context* ctx) {
    // Native probe frame pump for unk_0C0A70.c. The generated function busy-waits
    // on osGetCount until enough N64 ticks have elapsed, then calls
    // updateFrameCounters(). In the guarded host harness we advance that state
    // deterministically instead of burning CPU until the alarm fires.
    constexpr uint32_t kLastFrameCounter = 0x80048490u;
    constexpr uint32_t kCurrentFrameCounter = 0x80048494u;
    constexpr uint32_t kSpeedgraphFrames = 0x80048498u;
    constexpr uint32_t kPreviousFrameCounter = 0x8004849Cu;
    constexpr uint32_t kHalfFrameCounter = 0x800484A0u;
    constexpr uint32_t kIsFrameCounterOdd = 0x800484A4u;
    constexpr uint32_t kHalfMinusPreviousCounter = 0x800484A8u;
    constexpr uint32_t kOsCountCopy0 = 0x800484ACu;
    constexpr uint32_t kOsCountCopy1 = 0x800484B0u;
    constexpr uint32_t kFrameDelay = 0x800484B4u;
    constexpr uint32_t kNtSCFrameTicks = 775875u;

    int32_t delta_frames = read_runtime_word(rdram, kFrameDelay);
    if (delta_frames <= 0 || delta_frames > 10) {
        delta_frames = 1;
    }

    const int32_t last_current = read_runtime_word(rdram, kCurrentFrameCounter);
    const int32_t previous_half = read_runtime_word(rdram, kHalfFrameCounter);
    const int32_t next_current = last_current + delta_frames;
    const int32_t next_half = next_current / 2;

    write_runtime_word(rdram, kOsCountCopy0, read_runtime_word(rdram, kOsCountCopy1));
    g_host_frame_os_count += static_cast<uint32_t>(delta_frames) * kNtSCFrameTicks;
    write_runtime_word(rdram, kOsCountCopy1, static_cast<int32_t>(g_host_frame_os_count));
    write_runtime_word(rdram, kLastFrameCounter, last_current);
    write_runtime_word(rdram, kCurrentFrameCounter, next_current);
    write_runtime_word(rdram, kSpeedgraphFrames, delta_frames);
    write_runtime_word(rdram, kPreviousFrameCounter, previous_half);
    write_runtime_word(rdram, kHalfFrameCounter, next_half);
    write_runtime_word(rdram, kIsFrameCounterOdd, next_current & 1);
    write_runtime_word(rdram, kHalfMinusPreviousCounter, next_half - previous_half);
    write_runtime_word(rdram, kFrameDelay, 1);

    g_host_frame_ticks++;
    std::printf("host_frame_tick count=%u delta=%d currentFrameCounter=%d os_count=0x%08X\n",
        g_host_frame_ticks,
        delta_frames,
        next_current,
        g_host_frame_os_count);
    std::fflush(stdout);

    const uint32_t limit = frame_tick_limit();
    if (limit != 0 && g_host_frame_ticks >= limit) {
        std::fprintf(stderr,
            "host_frame_tick_limit reached count=%u currentFrameCounter=%d\n",
            g_host_frame_ticks,
            next_current);
        std::fflush(stderr);
        std::_Exit(150);
    }

    if (ctx != nullptr) {
        ctx->r2 = 0;
    }
}

void rspGfxTaskStart(uint8_t* rdram, recomp_context* ctx) {
    if (ctx == nullptr) {
        return;
    }

    constexpr int16_t kOsScDoneMsg = 2;
    constexpr uint32_t kGfxFrameMsgQueue = 0x8005D9A0u;
    const uint32_t first_gdl = static_cast<uint32_t>(ctx->r4);
    const uint32_t end_gdl = static_cast<uint32_t>(ctx->r5);
    const uint32_t flags = static_cast<uint32_t>(ctx->r6);
    const uint32_t done_msg = static_cast<uint32_t>(ctx->r7);

    g_host_rsp_tasks++;
    goldeneye_runtime_record_rsp_task_started();
    std::printf(
        "host_rsp_task_consume count=%u first_gdl=0x%08X end_gdl=0x%08X flags=0x%08X done_msg=0x%08X frame_ticks=%u\n",
        g_host_rsp_tasks,
        first_gdl,
        end_gdl,
        flags,
        done_msg,
        g_host_frame_ticks);

    const GoldenEyeRendererTaskResult renderer_result = goldeneye_renderer_execute_display_list_task(rdram, first_gdl, end_gdl);
    goldeneye_renderer_print_task_result(renderer_result);

    if (done_msg != 0) {
        write_runtime_half(rdram, done_msg, kOsScDoneMsg);
    }

    const bool queued_done = done_msg != 0 && enqueue_runtime_message(rdram, kGfxFrameMsgQueue, done_msg);
    std::printf("host_rsp_task_done_queued count=%u queue=0x%08X msg=0x%08X type=%d queued=%d limit=%zu\n",
        g_host_rsp_tasks,
        kGfxFrameMsgQueue,
        done_msg,
        done_msg != 0 ? static_cast<int>(read_runtime_half(rdram, done_msg)) : 0,
        queued_done ? 1 : 0,
        goldeneye_runtime_rsp_task_limit());
    std::fflush(stdout);

    ctx->r2 = queued_done ? 0 : -1;
}

void eqpower(uint8_t* rdram, recomp_context* ctx) {
    replaced_runtime_stub("eqpower", rdram, ctx);
}

} // extern "C"
