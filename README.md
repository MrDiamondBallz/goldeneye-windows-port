# GoldenEye / N64 Native Port Research

Research repo for turning legally supplied N64 inputs into native host executables, starting with **GoldenEye 007 (USA)**.

This is **not** an emulator frontend, and the repo does **not** ship game files. The current path is straightforward: user-owned N64 inputs, N64Recomp-generated C/C++, a native runtime harness, and eventually a proper desktop port.

## Status

| Area | Current state |
| --- | --- |
| N64Recomp codegen | GoldenEye USA codegen completes locally: `14380` functions, 18 generated files. |
| Generated code | Compiles and links into a Linux x86-64 host executable. Generated output stays ignored/local-only. |
| Runtime memory | Sparse 4 GiB+ host address space mirrors N64Recomp low-address aliasing for `0x700...`, `0x7F...`, and KSEG0 access patterns. |
| Boot path | Guarded execution reaches `recomp_entrypoint -> boot bridge -> generated init -> generated mainproc`. |
| Runtime primitives | First-pass ROM DMA, message queues, cooperative thread records, VI framebuffer bookkeeping, timing helpers, and guarded probes are implemented. |
| Current blocker | Guarded `mainproc` now advances through two host-simulated frame ticks, then reaches `guPerspectiveF` with missing view/camera state in the generated render path. |

Latest verified normal probe:

```text
controlled_probe_result=OK boot_primitives_enabled safe_generated_dispatch_enabled
next_runtime_blocker=guarded frame pump advances two ticks, then generated render path reaches guPerspectiveF with missing view/camera state
```

The game does **not** boot yet. The next milestone is valid view/camera/player bootstrap for the render path, plus real scheduler/video presentation behind the current probe-only frame tick.

## Legal boundary

This repository is code and documentation only. Do not commit:

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
GOLDENEYE_TRY_ENTRYPOINT=1 GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=20 GOLDENEYE_FRAME_TICK_LIMIT=3 ports/goldeneye/build-native-spike/goldeneye_native_spike
```

The guarded probe forks a child process, installs signal diagnostics, and uses an alarm so deeper generated-code execution cannot make the normal verification path unsafe. `GOLDENEYE_FRAME_TICK_LIMIT` can cap successful host-simulated frame ticks during runtime experiments.

## Current technical notes

- The original GoldenEye `boot` function mainly installs a low-address TLB mapping and jumps to `init`. The host runtime already supplies the aliasing, so the native bridge calls generated `init` directly.
- The generated inflate/TLB path is not complete yet. The guarded child restores the local decomp ELF `.csegment` at the `initTLBPrepareContext` seam so generated main-thread code sees resolved game data while this layer is under construction.
- `osCreateThread` / `osStartThread` currently record cooperative thread metadata. The guarded probe dispatches the recorded main thread (`id=3`, `mainproc`) once.
- Scheduler message reads can synthesize retrace messages for blocking waits, and `waitForNextFrame` is now a deterministic host frame tick that updates the original frame-counter globals.
- Debug registry, early audio, memory-pool resizing, and generic compressed-asset expansion are probe-only placeholders so the guarded path can advance to the next major runtime seam.
- Scheduler, video, audio, input, and controller behavior are still skeletal runtime replacements; the current guarded boundary is generated rendering setup at `guPerspectiveF`.

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
