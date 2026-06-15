#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ucontext.h>
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

uint8_t* g_signal_rdram = nullptr;

uint32_t read_u32(uint8_t* rdram, uint32_t vaddr) {
    return static_cast<uint32_t>(MEM_W(0, S32(vaddr)));
}

bool entrypoint_probe_requested() {
    const char* value = std::getenv("GOLDENEYE_TRY_ENTRYPOINT");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

unsigned entrypoint_probe_timeout_seconds() {
    const char* value = std::getenv("GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC");
    if (value == nullptr || value[0] == '\0') {
        return 2;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || parsed == 0 || parsed > 60) {
        return 2;
    }
    return static_cast<unsigned>(parsed);
}

void init_probe_context(recomp_context* ctx) {
    *ctx = recomp_context{};
    ctx->f_odd = &ctx->f1.u32l;
    ctx->mips3_float_mode = 1;
    ctx->r29 = S32(0x807FF000u);
}

void entrypoint_signal_handler(int signal_number, siginfo_t* info, void* raw_context) {
    uintptr_t pc = 0;
#if defined(__x86_64__)
    auto* uctx = reinterpret_cast<ucontext_t*>(raw_context);
    pc = static_cast<uintptr_t>(uctx->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
    auto* uctx = reinterpret_cast<ucontext_t*>(raw_context);
    pc = static_cast<uintptr_t>(uctx->uc_mcontext.pc);
#else
    (void)raw_context;
#endif
    Dl_info dl_info{};
    const bool has_symbol = pc != 0 && dladdr(reinterpret_cast<void*>(pc), &dl_info) != 0;
    const auto symbol_offset = (has_symbol && dl_info.dli_saddr != nullptr)
        ? static_cast<std::size_t>(pc - reinterpret_cast<uintptr_t>(dl_info.dli_saddr))
        : 0;
    const auto module_offset = (has_symbol && dl_info.dli_fbase != nullptr)
        ? static_cast<std::size_t>(pc - reinterpret_cast<uintptr_t>(dl_info.dli_fbase))
        : 0;
    const auto fault_delta = (g_signal_rdram != nullptr && info != nullptr && info->si_addr != nullptr)
        ? static_cast<long long>(reinterpret_cast<uint8_t*>(info->si_addr) - g_signal_rdram)
        : 0LL;
    std::fprintf(stderr,
        "entrypoint_child_signal signal=%d fault_addr=%p fault_rdram_delta=%lld pc=0x%zx module_offset=0x%zx symbol=%s+0x%zx object=%s\n",
        signal_number,
        info ? info->si_addr : nullptr,
        fault_delta,
        static_cast<std::size_t>(pc),
        module_offset,
        has_symbol && dl_info.dli_sname ? dl_info.dli_sname : "(unknown)",
        symbol_offset,
        has_symbol && dl_info.dli_fname ? dl_info.dli_fname : "(unknown)");
    std::fflush(stderr);
    _exit(128 + signal_number);
}

void install_entrypoint_signal_handlers() {
    struct sigaction action {};
    action.sa_sigaction = entrypoint_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
    sigaction(SIGALRM, &action, nullptr);
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

    std::fflush(stdout);
    const pid_t child = fork();
    if (child < 0) {
        std::printf("entrypoint_probe=blocked reason=fork_failed errno=%d\n", errno);
        return;
    }

    if (child == 0) {
        g_signal_rdram = rdram;
        install_entrypoint_signal_handlers();
        alarm(entrypoint_probe_timeout_seconds());
        recomp_context ctx{};
        init_probe_context(&ctx);
        entry(rdram, &ctx);
        std::printf("entrypoint_child=returned r2=0x%016llX sp=0x%016llX\n",
            static_cast<unsigned long long>(ctx.r2),
            static_cast<unsigned long long>(ctx.r29));
        std::printf("post_init_probe g_Textures[0]=0x%08X g_Textures[1]=0x%08X\n",
            read_u32(rdram, 0x80049300u),
            read_u32(rdram, 0x80049308u));
        goldeneye_runtime_print_thread_records();
        std::fflush(stdout);
        const std::size_t dispatched = goldeneye_runtime_dispatch_started_threads(rdram, 3, 1);
        std::printf("entrypoint_child_threads_dispatched=%zu\n", dispatched);
        goldeneye_runtime_print_thread_records();
        goldeneye_runtime_print_diagnostics();
        std::fflush(stdout);
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
    void* mapping = mmap(nullptr,
        kGoldenEyeHostAddressSpaceBytes,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
        -1,
        0);
    if (mapping == MAP_FAILED) {
        std::fprintf(stderr, "failed to reserve sparse host address space: errno=%d\n", errno);
        return 1;
    }

    uint8_t* rdram = static_cast<uint8_t*>(mapping);
    const char* rom_path = default_rom_path();

    GoldenEyeRuntimeState runtime_state{};
    const bool runtime_ok = goldeneye_runtime_init(rdram, kGoldenEyeRdramSize, rom_path, &runtime_state);

    std::printf("GoldenEye native boot harness spike\n");
    std::printf("rom_name=%s entry=0x%08llX generated_lookup=callable\n",
        get_rom_name(),
        static_cast<unsigned long long>(get_entrypoint_address()));
    std::printf("rom_path=%s\n", rom_path);
    std::printf("host_address_space=%zu logical_rdram=%zu low_alias_span=%zu runtime=%s\n",
        kGoldenEyeHostAddressSpaceBytes,
        kGoldenEyeRdramSize,
        kGoldenEyeLowMirrorBytes,
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
        {"init", 0x70000510u, false},
        {"decompress_entry", 0x7020141Cu, false},
        {"mainproc", 0x7000089Cu, false},
        {"idleproc", 0x70000718u, false},
        {"amDmaNew", 0x700025D8u, false},
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
    std::printf("next_runtime_blocker=host renderer shim scans generated display-list tasks and returns scheduler done messages; segmented display-list resolver plus RT64 backend integration is the next runtime layer\n");
    return 0;
}
