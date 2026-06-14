# GoldenEye / N64 Native Port Research

Experimental native-port research for running N64 games as real host executables, starting with **GoldenEye 007 (USA)**.

This is **not** an emulator frontend and it does **not** distribute game files. The current track is a ROM/decompilation-assisted static-recompilation pipeline: user-owned N64 inputs → N64Recomp-generated C/C++ → a native runtime harness → eventually a proper desktop port.

## Status

| Area | Current state |
| --- | --- |
| N64Recomp codegen | GoldenEye USA codegen completes locally: `14380` functions, 18 generated files. |
| Generated code | Compiles and links into a Linux x86-64 host executable. Generated output stays ignored/local-only. |
| Runtime memory | Sparse 4 GiB+ host address space mirrors N64Recomp low-address aliasing for `0x700...`, `0x7F...`, and KSEG0 access patterns. |
| Boot path | Guarded execution reaches `recomp_entrypoint -> boot bridge -> generated init -> generated mainproc`. |
| Runtime primitives | First-pass ROM DMA, message queues, cooperative thread records, VI framebuffer bookkeeping, timing helpers, and guarded probes are implemented. |
| Current blocker | Generated `mainproc` dispatch reaches the debug `debFind` / `pause_self` path and times out under the guarded child-process probe. |

Latest verified normal probe:

```text
controlled_probe_result=OK boot_primitives_enabled safe_generated_dispatch_enabled
next_runtime_blocker=main-thread dispatch reaches debFind/pause_self debug path after local ELF csegment restore
```

The game does **not** boot yet. The next milestone is to turn the debug `pause_self` path into a structured runtime blocker or seed the missing debug/assertion state so the main-thread probe can continue into scheduler/video/audio initialization.

## Legal boundary

This repository is code and documentation only.

It must not contain:

- commercial ROMs, ISOs, XEX files, or other copyrighted game binaries;
- extracted commercial assets, textures, audio, saves, or proprietary SDK material;
- generated game-derived code or generated native binaries;
- private credentials or machine-local artifacts.

Public-domain or explicitly redistributable ROM fixtures may be added only under `test-roms/public-domain/` with source and license notes. Commercial files must remain local and ignored.

## Repository layout

```text
assets/                         Local-only asset placeholder docs
patches/n64recomp/              N64Recomp patches required by the spike
ports/goldeneye/app/            Native host harness / executable entrypoint
ports/goldeneye/config/         N64Recomp config for GoldenEye USA
ports/goldeneye/runtime/        Minimal host runtime and libultra replacements
scripts/                        Codegen, build, analysis, and smoke-test helpers
docs/                           Assignment notes and longer-form technical plan
test-roms/                      Public-domain fixture policy
```

Ignored/generated paths include:

```text
ports/goldeneye/generated/
ports/goldeneye/build-native-spike/
assets/
*.z64 / *.n64 / *.v64 / *.rom / *.iso / *.xex
```

## Local prerequisites

The current spike expects local, legally obtained inputs and toolchains:

- GoldenEye 007 USA ROM, supplied by the user and kept outside git.
- Matching `n64decomp/007` build output, including `ge007.u.elf`.
- N64Recomp built locally.
- A Linux C/C++ build environment with CMake and GNU toolchain.

Useful environment overrides:

```bash
export GE007_ROM=/path/to/baserom.u.z64
export GE007_ELF=/path/to/ge007.u.elf
export N64RECOMP_DIR=/path/to/N64Recomp
```

## Build and verify

From the repository root:

```bash
scripts/build_goldeneye_native_spike.sh
```

That script:

1. applies the tracked N64Recomp patch if needed;
2. regenerates GoldenEye recompilation output locally;
3. configures the native harness with CMake;
4. builds `goldeneye_native_spike`;
5. runs the safe normal probe.

Optional guarded boot/main-thread probe:

```bash
GOLDENEYE_TRY_ENTRYPOINT=1 ports/goldeneye/build-native-spike/goldeneye_native_spike
```

The guarded probe forks a child process, installs signal diagnostics, and uses an alarm so deeper generated-code execution cannot make the normal verification path unsafe.

## Current technical notes

- The original GoldenEye `boot` function mainly installs a low-address TLB mapping and jumps to `init`. The host runtime already supplies the aliasing, so the native bridge calls generated `init` directly.
- The generated inflate/TLB path is not complete yet. The guarded child restores the local decomp ELF `.csegment` at the `initTLBPrepareContext` seam so generated main-thread code sees resolved game data while this layer is under construction.
- `osCreateThread` / `osStartThread` currently record cooperative thread metadata. The guarded probe dispatches the recorded main thread (`id=3`, `mainproc`) once.
- `pause_self`, scheduler, video, audio, input, and controller behavior are still skeletal runtime replacements.

## Documentation

- [`docs/N64_ROM_NATIVE_PORT_ASSIGNMENT.md`](docs/N64_ROM_NATIVE_PORT_ASSIGNMENT.md) — detailed running log, verified outputs, and next blockers.
- [`docs/GOLDENEYE_PC_PORT_PLAN.md`](docs/GOLDENEYE_PC_PORT_PLAN.md) — broader native-port direction.

## Contributing

This project is early-stage research. Keep changes small, verified, and legally clean.

Before opening or merging changes:

```bash
git status --short --ignored
scripts/build_goldeneye_native_spike.sh
```

Also verify that no ROMs, generated game output, binaries, credentials, or machine-local paths are staged.
