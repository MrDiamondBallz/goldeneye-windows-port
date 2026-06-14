#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/wait.h>
#include <unistd.h>

#include "recomp.h"
#include "funcs.h"
#include "runtime.h"

gpr get_entrypoint_address();
const char* get_rom_name();

namespace {

const char* default_rom_path() {
    if (const char* from_env = std::getenv("GE007_ROM")) {
        return from_env;
    }
    return "/root/projects/007/baserom.u.z64";
}

bool entrypoint_probe_requested() {
    const char* value = std::getenv("GOLDENEYE_TRY_ENTRYPOINT");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

void init_probe_context(recomp_context* ctx) {
    *ctx = recomp_context{};
    ctx->mips3_float_mode = 1;
    ctx->r29 = S32(0x807FF000u);
}

void maybe_run_guarded_entrypoint(uint8_t* rdram) {
    if (!entrypoint_probe_requested()) {
        std::printf("entrypoint_probe=skipped set GOLDENEYE_TRY_ENTRYPOINT=1 to attempt guarded child process\n");
        return;
    }

    recomp_func_t* entry = goldeneye_lookup_function(0x80000400u);
    if (entry == nullptr) {
        std::printf("entrypoint_probe=blocked reason=lookup_not_enabled\n");
        return;
    }

    const pid_t child = fork();
    if (child < 0) {
        std::printf("entrypoint_probe=blocked reason=fork_failed errno=%d\n", errno);
        return;
    }

    if (child == 0) {
        alarm(2);
        recomp_context ctx{};
        init_probe_context(&ctx);
        entry(rdram, &ctx);
        std::printf("entrypoint_child=returned r2=0x%016llX sp=0x%016llX\n",
            static_cast<unsigned long long>(ctx.r2),
            static_cast<unsigned long long>(ctx.r29));
        _exit(0);
    }

    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        std::printf("entrypoint_probe=blocked reason=waitpid_failed errno=%d\n", errno);
        return;
    }

    if (WIFEXITED(status)) {
        std::printf("entrypoint_probe=attempted child_exit=%d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        std::printf("entrypoint_probe=attempted child_signal=%d\n", WTERMSIG(status));
    } else {
        std::printf("entrypoint_probe=attempted child_status=0x%X\n", status);
    }
}

} // namespace

int main() {
    auto backing = std::make_unique<uint8_t[]>(kGoldenEyeLowMirrorBytes + kGoldenEyeRdramSize);
    std::memset(backing.get(), 0, kGoldenEyeLowMirrorBytes + kGoldenEyeRdramSize);
    uint8_t* rdram = backing.get() + kGoldenEyeLowMirrorBytes;
    const char* rom_path = default_rom_path();

    GoldenEyeRuntimeState runtime_state{};
    const bool runtime_ok = goldeneye_runtime_init(rdram, kGoldenEyeRdramSize, rom_path, &runtime_state);

    std::printf("GoldenEye native boot harness spike\n");
    std::printf("rom_name=%s entry=0x%08llX generated_lookup=callable\n",
        get_rom_name(),
        static_cast<unsigned long long>(get_entrypoint_address()));
    std::printf("rom_path=%s\n", rom_path);
    std::printf("memory_layout=low_mirror:%zu+rdram:%zu runtime=%s\n",
        kGoldenEyeLowMirrorBytes,
        kGoldenEyeRdramSize,
        runtime_ok ? "boot-primitives" : "init-failed");

    if (!runtime_ok) {
        return 1;
    }

    goldeneye_runtime_print_state(runtime_state);

    struct MetadataProbe {
        const char* name;
        uint32_t vram;
        bool auto_call;
    } lookups[] = {
        {"recomp_entrypoint", 0x80000400u, false},
        {"boot", 0x80000450u, false},
        {"get_csegmentSegmentStart", 0x700004BCu, true},
        {"return_null", 0x7F06C46Cu, true},
    };

    for (const MetadataProbe& lookup : lookups) {
        recomp_func_t* func = goldeneye_lookup_function(lookup.vram);
        std::printf("metadata %s 0x%08X -> %s dispatch=%s\n",
            lookup.name,
            lookup.vram,
            goldeneye_has_function_metadata(lookup.vram) ? "FOUND" : "MISSING",
            func ? "ENABLED" : "DEFERRED");

        if (func != nullptr && lookup.auto_call) {
            recomp_context ctx{};
            init_probe_context(&ctx);
            func(rdram, &ctx);
            std::printf("probe %s -> OK r2=0x%016llX sp=0x%016llX\n",
                lookup.name,
                static_cast<unsigned long long>(ctx.r2),
                static_cast<unsigned long long>(ctx.r29));
        }
    }

    // Exercise the first boot-grade primitives without entering full game boot.
    uint32_t queue_addr = 0x807FE000u;
    uint32_t queue_buf = 0x807FE100u;
    uint32_t recv_slot = 0x807FE200u;
    recomp_context qctx{};
    init_probe_context(&qctx);
    qctx.r4 = S32(queue_addr);
    qctx.r5 = S32(queue_buf);
    qctx.r6 = 4;
    osCreateMesgQueue_recomp(rdram, &qctx);
    qctx.r4 = S32(queue_addr);
    qctx.r5 = S32(0x12345678u);
    qctx.r6 = 0;
    osSendMesg_recomp(rdram, &qctx);
    qctx.r4 = S32(queue_addr);
    qctx.r5 = S32(recv_slot);
    qctx.r6 = 0;
    osRecvMesg_recomp(rdram, &qctx);

    recomp_context dctx{};
    init_probe_context(&dctx);
    dctx.r7 = 0x00001000u;
    MEM_W(0x10, dctx.r29) = S32(0x807FD000u);
    MEM_W(0x14, dctx.r29) = 0x40;
    MEM_W(0x18, dctx.r29) = S32(queue_addr);
    osPiStartDma_recomp(rdram, &dctx);

    recomp_context tctx{};
    init_probe_context(&tctx);
    tctx.r4 = S32(0x807FC000u);
    tctx.r5 = 1;
    tctx.r6 = S32(0x80000400u);
    tctx.r7 = 0;
    MEM_W(0x10, tctx.r29) = S32(0x807FB000u);
    MEM_W(0x14, tctx.r29) = 10;
    osCreateThread_recomp(rdram, &tctx);
    tctx.r4 = S32(0x807FC000u);
    osStartThread_recomp(rdram, &tctx);

    goldeneye_runtime_print_diagnostics();
    maybe_run_guarded_entrypoint(rdram);

    std::printf("controlled_probe_result=OK boot_primitives_enabled safe_generated_dispatch_enabled\n");
    std::printf("next_runtime_blocker=replace cooperative stubs with accurate scheduler/video/audio paths after guarded entrypoint diagnostics\n");
    return 0;
}
