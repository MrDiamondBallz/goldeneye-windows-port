# GoldenEye / N64 Native Windows Port

Public project repo for building real native Windows `.exe` ports from legally usable N64 ROM inputs, starting with GoldenEye 007.

## Scope

- Native Windows executable, not an emulator frontend.
- Input starts from original/user-owned N64 ROMs where possible.
- Controller support and settings UI.
- Online multiplayer with custom netcode when the original game has no LAN/System Link protocol.
- Public-domain / explicitly redistributable ROM fixtures may be committed under `test-roms/public-domain/` with source/license notes; commercial copyrighted game files, extracted assets, generated game-derived code, and binary game artifacts stay out of git.

## Current assignment

The project direction was clarified that the target is **not** the XBLA/ReXGlue shortcut as the main path. The real goal is:

> Take any OG N64 ROM and make it a proper native Windows port. Custom netcode is fine.

So this repo now tracks the N64 ROM → N64Recomp/native-runtime route as the canonical project direction.

See:

- [`docs/N64_ROM_NATIVE_PORT_ASSIGNMENT.md`](docs/N64_ROM_NATIVE_PORT_ASSIGNMENT.md)
- [`docs/GOLDENEYE_PC_PORT_PLAN.md`](docs/GOLDENEYE_PC_PORT_PLAN.md)

## Legal boundary

This repo may contain public-domain or explicitly redistributable ROM fixtures under `test-roms/public-domain/` when source/license notes are included. It must not contain commercial copyrighted ROMs, XEX files, extracted game assets, generated recompiled code from copyrighted binaries, save dumps, textures, audio, proprietary SDK material, bundled game executables, or generated copyrighted artifacts.

Required user-owned files are placed locally only under ignored paths such as `assets/`, `generated/`, or external working directories.

## Local verified state

- User-supplied GoldenEye USA `.z64` verified by SHA1.
- `n64decomp/007` matching US ROM output exists locally.
- `N64Recomp` CLI tools build successfully locally.
- GoldenEye US N64Recomp codegen now completes with the tracked rodata jump-table patch and current config.
- The generated code pass reports `14380` functions and emits 18 generated files locally under ignored `ports/goldeneye/generated/us_recomp/`.
- Native host spike now compiles/links generated GoldenEye code into a Linux x86-64 executable with a stub runtime.
- Boot harness now allocates a low-address host mirror and maps GoldenEye's `0x700...` / `0x7F...` sections instead of skipping them.
- First boot-grade runtime primitives are enabled: ROM DMA copy, message queues, cooperative thread records, VI framebuffer bookkeeping, and guarded entrypoint probing.
- Latest verified native spike output: `controlled_probe_result=OK boot_primitives_enabled safe_generated_dispatch_enabled`.
- Guarded `GOLDENEYE_TRY_ENTRYPOINT=1` probe reaches the `boot` replacement seam and exits cleanly; the real boot body is still intentionally stubbed.
- `N64ModernRuntime` and `RT64` are cloned for native runtime work.
- RecompFrontend needs a proper consuming app/CMake scaffold before it can be built standalone.

## Immediate next milestone

Replace the `boot` seam and cooperative stubs with accurate scheduler/video/audio paths, then let guarded `recomp_entrypoint` progress into real game initialization.
