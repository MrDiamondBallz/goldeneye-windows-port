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
| Current blocker | Guarded `mainproc` now reaches bounded RSP/display-list tasks; renderer refs are checked against a runtime resource-provenance table for ROM DMA, cdata, csegment restore, memp allocations, and decompression attempts before scheduler done messages return to the game loop. |

Latest verified normal probe:

```text
controlled_probe_result=OK boot_primitives_enabled safe_generated_dispatch_enabled
next_runtime_blocker=resource provenance now backs renderer refs; next layer is real memp pool initialization/decompression provenance before RT64/custom rendering
```

The game does **not** boot yet. The next milestone is real memp pool initialization/decompression provenance, then translating only proven F3DEX/RDP packets into RT64 or a custom presentation backend.

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
GOLDENEYE_TRY_ENTRYPOINT=1 GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=20 ports/goldeneye/build-native-spike/goldeneye_native_spike
```

The guarded probe forks a child process, installs signal diagnostics, and uses an alarm so deeper generated-code execution cannot make the normal verification path unsafe. `GOLDENEYE_FRAME_TICK_LIMIT` can cap successful host-simulated frame ticks during runtime experiments. `GOLDENEYE_RSP_TASK_LIMIT=N` controls how many generated display-list tasks the host shim consumes before stopping; default is `1`. `GOLDENEYE_RENDERER_COMMAND_LIMIT`, `GOLDENEYE_RENDERER_LIST_COMMAND_LIMIT`, and `GOLDENEYE_RENDERER_DEPTH_LIMIT` bound recursive display-list walking. `GOLDENEYE_CONTINUE_AFTER_RSP_TASK=1` disables the RSP done-message stop, but that path is intentionally not the default because the host renderer is still skeletal.

## Current technical notes

- The original GoldenEye `boot` function mainly installs a low-address TLB mapping and jumps to `init`. The host runtime already supplies the aliasing, so the native bridge calls generated `init` directly.
- The generated inflate/TLB path is not complete yet. The guarded child restores the local decomp ELF `.csegment` at the `initTLBPrepareContext` seam so generated main-thread code sees resolved game data while this layer is under construction.
- `osCreateThread` / `osStartThread` currently record cooperative thread metadata. The guarded probe dispatches the recorded main thread (`id=3`, `mainproc`) once.
- Probe contexts now initialize the N64Recomp odd-FPR pointer (`f_odd`) for MIPS3 float mode, which clears the previous `guPerspectiveF` crash.
- Scheduler message reads can synthesize retrace messages for blocking waits, and `waitForNextFrame` is now a deterministic host frame tick that updates the original frame-counter globals.
- Debug registry, early audio, and generic compressed-asset expansion are probe-only placeholders so the guarded path can advance to the next major runtime seam. `mempAllocBytesInBank` / `mempAddEntryOfSizeToBank` now read/write the GoldenEye `g_mempPools` layout when usable and otherwise record explicit host-scratch/failed allocation provenance. `decompressdata` also records default stub attempts and has an opt-in generated-inflater bridge (`GOLDENEYE_ENABLE_DECOMPRESS_BRIDGE=1`) for experiments; it currently reaches a guarded child alarm before renderer task submission, so the default path keeps the known renderer boundary stable.
- The host renderer shim now recursively walks generated display-list tasks (`host_renderer_execute`) with bounded command/list/depth limits, classifies RSP/RDP presentation commands, prints a top-opcode histogram, validates backend resource addresses, previews texture image candidates with command/list/branch/depth provenance, and emits backend packet records (`host_renderer_backend_packet[...]`) with opcode names, raw words, resolved addresses, translation validity, backed-resource state, resource kind, bank, and recorded size. Branch targets are probed before recursion; payload/data-like targets are skipped by default and can be force-scanned with `GOLDENEYE_RENDERER_SCAN_PAYLOAD_BRANCHES=1`. The latest guarded first task scans only the top-level `303` commands, identifies `3` branch targets as `payload_or_unknown`, skips all `3`, reports `host_renderer_branch_summary targets=3 plausible=0 payload_or_unknown=3 untranslated=0 skipped=3 scheduled=0 payload_scan_enabled=0`, classifies `2` malformed display-list image commands and `0` payload false positives in the default guarded path, validates `10` of `10` backend address-bearing refs, and proves `6` are resource-backed (`memp_alloc` or `rom_dma`) while `4` are translated-but-unbacked. The guarded one-task resource summary reaches `total=90 bytes=7489792 rom_dma=23 cdata=1 csegment=1 memp_alloc=65 memp_resize=0 decompress_stub=0`, queues `OS_SC_DONE_MSG` back into `gfxFrameMsgQ`, and stops after the bounded done message. A two-task guarded probe (`GOLDENEYE_RSP_TASK_LIMIT=2`) advances through two scheduler-done cycles with the same branch/resource-classification shape. Opt-in payload scanning preserves the forensic over-walk path. The current blocker is real memp pool initialization/decompression provenance before RT64/custom rendering, scheduler, video, audio, input, and controller behavior are completed.

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
