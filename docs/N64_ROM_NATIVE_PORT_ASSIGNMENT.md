# N64 ROM → Native Windows Port Assignment

## Current marching orders

The project direction was clarified the real assignment:

> Be able to take any OG N64 ROM and make it a proper native Windows port. Custom netcode is fine. Build it. Make no mistakes.

So the canonical route for this project is now the **N64 ROM / N64Recomp route**, not the XBLA/ReXGlue shortcut.

## Non-negotiables

- Input: legally owned original N64 ROMs (`.z64`, `.n64`, `.v64`) supplied locally by the user.
- Output: native Windows `.exe`, not an emulator frontend and not a RetroArch/Project64 wrapper.
- Controller support: SDL2/RecompFrontend-style gamepad binding and multiplayer profiles.
- Online play: custom netcode is allowed; GoldenEye starts with local split-screen semantics, so online requires engineered synchronization.
- Repo hygiene: public-domain / explicitly redistributable ROM fixtures are allowed with source/license notes; commercial copyrighted ROMs, extracted copyrighted assets, generated copyrighted game code, save files, and binary game artifacts stay out of git.

## Ground truth verified locally

### GoldenEye ROM

User-supplied archive contained:

```text
GoldenEye 007 (USA).z64
```

Verified:

```text
SHA1   abe01e4aeb033b6c0836819f549c791b26cfde83
SHA256 2cdcec8a9f0cb6e36337f3ee39d8ad105dc8afa6ba1c02d466e8f5b771f9a162
Size   12,582,912 bytes
Order  big-endian .z64 native N64 order
Title  GOLDENEYE
Region USA
```

Working copy:

```text
/root/.hermes/cache/documents/goldeneye_007_verified/GoldenEye 007 (USA).z64
```

### N64 decomp baseline

Repo:

```text
/root/projects/007
https://github.com/n64decomp/007
```

Verified matching output exists:

```text
/root/projects/007/build/u/ge007.u.z64
SHA1 abe01e4aeb033b6c0836819f549c791b26cfde83
```

This proves the local decomp/baserom path is sane, but it is still a matching N64 ROM build, not a PC port.

### Native recomp toolchain

N64Recomp is cloned and built:

```text
/root/projects/goldeneye-pc-port/N64Recomp/build/N64Recomp
/root/projects/goldeneye-pc-port/N64Recomp/build/RSPRecomp
/root/projects/goldeneye-pc-port/N64Recomp/build/RecompModTool
/root/projects/goldeneye-pc-port/N64Recomp/build/RecompModMerger
/root/projects/goldeneye-pc-port/N64Recomp/build/OfflineModRecomp
```

N64ModernRuntime and RT64 have also been cloned for frontend/runtime integration:

```text
/root/projects/goldeneye-pc-port/N64ModernRuntime
/root/projects/goldeneye-pc-port/RT64
```

RecompFrontend configuration currently fails standalone because its shader helper CMake functions (`build_vertex_shader`, `build_pixel_shader`) are expected from a consuming project/RT64 integration layer, not from the bare frontend root as invoked. That needs a real port-app CMake scaffold, not blind retries.

## Generic pipeline target

For each supported N64 game/revision:

1. **ROM preflight**
   - Detect byte order and normalize to `.z64` if needed.
   - Verify known-good hash/revision.
   - Refuse unknown or mismatched ROMs unless explicitly accepted as an unsupported spike.

2. **Metadata acquisition**
   - Prefer existing legal decomp/disassembly outputs where available.
   - Produce/reuse ELF/symbol metadata required by N64Recomp.
   - Record segments, overlays, entrypoints, and relocations.

3. **Static recompilation**
   - Generate C/C++ from the ROM/ELF metadata using N64Recomp.
   - Recompile RSP microcode where needed.
   - Keep generated game-derived code out of public git.

4. **Runtime integration**
   - Link generated code to N64ModernRuntime/RT64-style runtime.
   - Implement graphics/audio/input/save/timing boundaries.
   - Use RecompFrontend or equivalent for controller mapping and menus.

5. **Game-specific patches/hooks**
   - Add hooks for menus, framerate, settings, input, save paths, and bug fixes.
   - Maintain a clean patch layer separate from generated output.

6. **Custom netcode**
   - Start with deterministic lockstep remote-input for GoldenEye multiplayer.
   - Add state hashing/desync detection before claiming online works.
   - Later evaluate host-authoritative entity replication only if lockstep fails.

7. **Windows packaging**
   - Build native Win64 `.exe`.
   - Package legal open-source runtime DLLs only.
   - Require user-owned ROM/assets at first launch or local build time.

8. **Verification**
   - Build output must compile.
   - Boot path must run.
   - Controller input must be observed.
   - Online claims require two-client local or LAN test with logs.
   - No copyrighted assets/artifacts committed.

## GoldenEye first milestone ladder

### Milestone 0 — Baseline proof

Status: mostly done.

- [x] Verify user-supplied GoldenEye USA ROM.
- [x] Build matching `n64decomp/007` output.
- [x] Build N64Recomp CLI tools.
- [ ] Produce/locate the exact ELF/symbol/overlay metadata N64Recomp needs for GoldenEye.

### Milestone 1 — Minimal native boot spike

Goal: compile a native executable that enters GoldenEye code through runtime glue and reaches a controlled boot/test point.

Deliverables:

- `ports/goldeneye/` scaffold.
- N64Recomp config TOML(s) for GoldenEye US.
- Generated code build path, ignored by git.
- Native Linux build first, then Win64 cross/native build.

### Milestone 2 — Rendering/input shell

Goal: actual window, RT64 rendering path, SDL2 controller input, settings menu.

Deliverables:

- App CMake project that provides required shader build helpers.
- RecompFrontend integration.
- Controller profile persistence.

### Milestone 3 — Multiplayer online spike

Goal: prove remote 2-player GoldenEye can synchronize beyond local split-screen assumptions.

Deliverables:

- Two local clients.
- Lockstep input exchange.
- Deterministic frame pacing.
- State hash/desync log.
- Relay/matchmaker process.

### Milestone 4 — Windows package

Goal: a Windows `.exe` package the player can launch.

Deliverables:

- `GoldenEyeNative.exe` or equivalent.
- Controller defaults.
- Host/join menu or config file.
- Dedicated relay/server executable if needed.
- No commercial copyrighted content bundled; public-domain / explicitly redistributable fixtures must carry source/license notes.

## Immediate next engineering task

Build the native app/runtime shell around generated GoldenEye C output. The first metadata/codegen bridge now exists; the next honest blocker is no longer codegen, it is runtime integration.

## N64Recomp config spike — codegen bridge status

The repo has a repeatable GoldenEye US codegen spike:

```bash
scripts/analyze_goldeneye_recomp_metadata.py
scripts/run_goldeneye_n64recomp_spike.sh
```

Inputs are the verified local GoldenEye US ELF/ROM from `/root/projects/007`.

Current verified result:

```text
Function count: 14380
[Info] Indirect tail call in recomp_entrypoint
[Info] Indirect tail call in jump_decompressfile
Generated summary:
exists True
file_count 18
total_bytes 27335105
```

The original blocker was N64Recomp assuming switch/jump tables live in the same section as the function body. GoldenEye places some tables in `.rodata`/data sections, e.g.:

```text
sizepropdef:      table 0x80053490, jr 0x7F056914, .rodata build/u/src/game/loadobjectmodel.o
texInflateLookup: table 0x8005BDE8, jr 0x7F0CA900, .rodata build/u/src/game/image.o
```

A local N64Recomp patch is tracked at:

```text
patches/n64recomp/0001-resolve-jump-tables-from-rodata-section.patch
```

The spike runner applies that patch and rebuilds N64Recomp before running codegen.

Remaining intentional ignores in `ge007_us_recomp.toml`:

- `sizepropdef` — kept as a tracked exception until this gameplay function is runtime-verified after the jump-table fix.
- `boot` — low-level N64 boot/PI/cop0 setup; native runtime replaces this.
- `resolve_TLBaddress_for_InvalidHit` — TLB/cop0 exception handler; native runtime replaces/reimplements this.
- `initTLBPrepareContext` — TLB/cop0 setup; native runtime replaces/reimplements this.
- `eqpower` — ELF marks it as `FUNC`, but disassembly shows a packed lookup/data table.

Next action: create the native app scaffold that compiles the generated C against a real `recomp.h`/runtime shim, then replace the ignored hardware/TLB functions with native runtime implementations where needed.

## Native executable scaffold status

A first host executable scaffold now exists:

```text
ports/goldeneye/app/CMakeLists.txt
ports/goldeneye/app/main.cpp
ports/goldeneye/runtime/recomp.h
ports/goldeneye/runtime/runtime.cpp
ports/goldeneye/runtime/stubs.cpp
scripts/build_goldeneye_native_spike.sh
```

Verified command:

```bash
scripts/build_goldeneye_native_spike.sh
```

Verified output:

```text
GoldenEye native spike
rom=ge007.u.z64 entry=0xFFFFFFFF80000400 generated_code=linked
rdram=8388608 bytes runtime=stub
```

The produced local binary is ignored and not committed:

```text
ports/goldeneye/build-native-spike/goldeneye_native_spike
SHA256 57726276d273eb9604cb12077513ddc20a9dd80cf712c4a51f7f0bdaf9efd927
```

This proves the generated GoldenEye translation units compile and link into a native Linux x86-64 host executable against a stub runtime. It does **not** boot the game yet. The next blocker is runtime correctness: segment loading, RDRAM layout, hardware/TLB replacements, and a controlled call into GoldenEye code.

