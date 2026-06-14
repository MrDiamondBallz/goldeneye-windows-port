#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "recomp.h"
#include "funcs.h"

gpr get_entrypoint_address();
const char* get_rom_name();

int main() {
    constexpr std::size_t rdram_size = 8 * 1024 * 1024;
    auto rdram = std::make_unique<uint8_t[]>(rdram_size);
    recomp_context ctx{};
    ctx.mips3_float_mode = 1;
    ctx.status_reg = 0;

    std::printf("GoldenEye native spike\n");
    std::printf("rom=%s entry=0x%08llX generated_code=linked\n",
        get_rom_name(),
        static_cast<unsigned long long>(get_entrypoint_address()));
    std::printf("rdram=%zu bytes runtime=stub\n", rdram_size);

    // Do not call recomp_entrypoint yet. The current deliverable proves that the
    // generated GoldenEye translation units compile/link against a host runtime
    // shim; actual boot requires segment loading and hardware/runtime replacements.
    return 0;
}
