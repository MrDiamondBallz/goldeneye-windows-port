#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "recomp.h"
#include "funcs.h"
#include "runtime.h"

gpr get_entrypoint_address();
const char* get_rom_name();

namespace {

constexpr std::size_t kRdramSize = 8 * 1024 * 1024;

const char* default_rom_path() {
    if (const char* from_env = std::getenv("GE007_ROM")) {
        return from_env;
    }
    return "/root/projects/007/baserom.u.z64";
}

} // namespace

int main() {
    auto rdram = std::make_unique<uint8_t[]>(kRdramSize);
    const char* rom_path = default_rom_path();

    GoldenEyeRuntimeState runtime_state{};
    const bool runtime_ok = goldeneye_runtime_init(rdram.get(), kRdramSize, rom_path, &runtime_state);

    std::printf("GoldenEye native boot harness spike\n");
    std::printf("rom_name=%s entry=0x%08llX generated_lookup=callable\n",
        get_rom_name(),
        static_cast<unsigned long long>(get_entrypoint_address()));
    std::printf("rom_path=%s\n", rom_path);
    std::printf("rdram=%zu bytes runtime=%s\n", kRdramSize, runtime_ok ? "segment-loader" : "init-failed");

    if (!runtime_ok) {
        return 1;
    }

    goldeneye_runtime_print_state(runtime_state);

    struct MetadataProbe {
        const char* name;
        uint32_t vram;
    } lookups[] = {
        {"recomp_entrypoint", 0x80000400u},
        {"get_csegmentSegmentStart", 0x700004BCu},
        {"return_null", 0x7F06C46Cu},
    };

    for (const MetadataProbe& lookup : lookups) {
        recomp_func_t* func = goldeneye_lookup_function(lookup.vram);
        std::printf("metadata %s 0x%08X -> %s dispatch=%s\n",
            lookup.name,
            lookup.vram,
            goldeneye_has_function_metadata(lookup.vram) ? "FOUND" : "MISSING",
            func ? "ENABLED" : "DEFERRED");

        if (func != nullptr) {
            recomp_context ctx{};
            ctx.mips3_float_mode = 1;
            ctx.r29 = S32(0x807FF000u);
            func(rdram.get(), &ctx);
            std::printf("probe %s -> OK r2=0x%016llX sp=0x%016llX\n",
                lookup.name,
                static_cast<unsigned long long>(ctx.r2),
                static_cast<unsigned long long>(ctx.r29));
        }
    }

    std::printf("controlled_probe_result=OK safe_generated_dispatch_enabled segment_loader_initialized\n");
    std::printf("next_runtime_blocker=implement broader libultra/hardware replacement coverage before recomp_entrypoint\n");
    return 0;
}
