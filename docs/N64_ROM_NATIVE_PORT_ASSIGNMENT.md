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
GoldenEye native boot harness spike
rom_name=ge007.u.z64 entry=0xFFFFFFFF80000400 generated_lookup=callable
host_address_space=4303355904 logical_rdram=8388608 low_alias_span=268435456 runtime=boot-primitives
sections: copied=4 skipped=1 copied_bytes=1068160 low_mapped=3 cdata_preloaded=78240 csegment_preloaded=0
metadata recomp_entrypoint 0x80000400 -> FOUND dispatch=DEFERRED
metadata boot 0x80000450 -> FOUND dispatch=ENABLED
metadata init 0x70000510 -> FOUND dispatch=ENABLED
metadata decompress_entry 0x7020141C -> FOUND dispatch=ENABLED
metadata mainproc 0x7000089C -> FOUND dispatch=ENABLED
metadata idleproc 0x70000718 -> FOUND dispatch=ENABLED
metadata amDmaNew 0x700025D8 -> FOUND dispatch=ENABLED
metadata get_csegmentSegmentStart 0x700004BC -> FOUND dispatch=ENABLED
probe get_csegmentSegmentStart -> OK r2=0xFFFFFFFF80020D90 sp=0xFFFFFFFF807FF000
metadata return_null 0x7F06C46C -> FOUND dispatch=ENABLED
probe return_null -> OK r2=0x0000000000000000 sp=0xFFFFFFFF807FF000
runtime_primitives: rom_bytes=12582912 dma_copies=6 dma_bytes=1146464 queues_created=1 messages_sent=2 messages_received=1 threads_created=1 threads_started=1 threads_dispatched=0 rsp_tasks_started=0 rsp_done_messages_delivered=0
entrypoint_probe=skipped set GOLDENEYE_TRY_ENTRYPOINT=1 to attempt guarded child process
controlled_probe_result=OK boot_primitives_enabled safe_generated_dispatch_enabled
next_runtime_blocker=renderer branch guard now skips payload/data branch targets by default; next layer is real memp/resource provenance before RT64/custom rendering
```

Guarded entrypoint/main-thread probe:

```text
GOLDENEYE_TRY_ENTRYPOINT=1 GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=20 ports/goldeneye/build-native-spike/goldeneye_native_spike
runtime replacement stub called: initTLBPrepareContext
entrypoint_child=returned r2=0x0000000000000000 sp=0xFFFFFFFF803AB410
post_init_probe g_Textures[0]=0x54070000 g_Textures[1]=0x6A010000
thread_records count=2
thread_dispatch id=3 entry=0x7000089C stack=0x803B3948 priority=10 dispatch=ENABLED
host_frame_tick count=1 delta=1 currentFrameCounter=1 os_count=0x000BD6C3
host_frame_tick count=2 delta=1 currentFrameCounter=2 os_count=0x0017AD86
host_rsp_task_consume count=1 first_gdl=0x8011B320 end_gdl=0x8011BC98 flags=0x00000000 done_msg=0x803B38EC frame_ticks=2
host_renderer_execute first_gdl=0x8011B320 end_gdl=0x8011BC98 bytes=0x978 top_commands=303 scanned=303 lists=1 max_depth=0 branch_dl=3 segmented_refs=3 resolved_segmented_refs=3 unresolved_refs=0 branch_scanned=0 rsp_commands=26 rdp_commands=277 enddl=1 cycles=0 limit_hit=0 list_limit_hit=0 depth_limit_hit=0
host_renderer_branch_target source=0x8011B330 target=0x80100040 depth=1 classification=payload_or_unknown readable=32 known=6 unknown=26 enddl=0 unreadable=0 scheduled=0 first_ops=02,01,01,02,02,01,02,03
host_renderer_branch_target source=0x8011B338 target=0x80100020 depth=1 classification=payload_or_unknown readable=32 known=6 unknown=26 enddl=0 unreadable=0 scheduled=0 first_ops=00,00,00,00,02,01,01,02
host_renderer_branch_target source=0x8011BC70 target=0x8011CA50 depth=1 classification=payload_or_unknown readable=32 known=2 unknown=30 enddl=0 unreadable=0 scheduled=0 first_ops=00,02,1D,01,24,01,00,00
host_renderer_branch_summary targets=3 plausible=0 payload_or_unknown=3 untranslated=0 skipped=3 scheduled=0 payload_scan_enabled=0
host_renderer_presentation matrix=7 vertex=0 texture=3 triangles=0 geom_mode=7 tex_images=0 tex_malformed=2 tex_segmented=0 tex_resolved=0 tex_unresolved=0 color_images=2 depth_images=1 tile_setup=218 texture_loads=2 combine=0 sync=11 fill_rect=2 othermode=20 packets=4
host_renderer_texture_classification raw_candidates=2 real_dl=0 malformed_dl=2 false_payload=0 backed=0 real_unbacked=0
host_renderer_backend packets=273 geometry=7 state=30 texture=220 target=5 sync=11 address_refs=10 valid_refs=10 invalid_refs=0
host_renderer_opcode_histogram op00=154 opF5=114 op6C=112 opF2=112 op93=78 op20=40 op03=34 op01=29 op02=28 opBC=22 op40=20 opBA=20
host_renderer_backend_packet[0]=pipe_sync op=0xE7 w0=0xE7000000 w1=0x00000000 resolved=0x00000000 valid=0
host_renderer_backend_packet[1]=set_depth_image op=0xFE w0=0xFE000000 w1=0x80142440 resolved=0x80142440 valid=1
host_renderer_backend_packet[2]=pipe_sync op=0xE7 w0=0xE7000000 w1=0x00000000 resolved=0x00000000 valid=0
host_renderer_backend_packet[3]=set_othermode_l op=0xB9 w0=0xB900031D w1=0x00000000 resolved=0x00000000 valid=0
host_renderer_backend_packet[4]=set_color_image op=0xFF w0=0xFF10013F w1=0x00142440 resolved=0x80142440 valid=1
host_renderer_backend_packet[5]=set_othermode_h op=0xBA w0=0xBA001402 w1=0x00300000 resolved=0x00000000 valid=0
host_renderer_backend_packet[6]=fill_rect op=0xF6 w0=0xF60003BC w1=0x00000000 resolved=0x00000000 valid=0
host_renderer_backend_packet[7]=pipe_sync op=0xE7 w0=0xE7000000 w1=0x00000000 resolved=0x00000000 valid=0
host_renderer_dlist[0]=0xBC000006_00000000
host_renderer_dlist[1]=0xBC000406_00100000
host_renderer_dlist[2]=0x06000000_01000040
host_renderer_dlist[3]=0x06000000_01000020
host_renderer_branch_dlist[0]=0xBC000404_00000002
host_renderer_branch_dlist[1]=0xBC000C04_00000002
host_renderer_branch_dlist[2]=0xBC001404_0000FFFE
host_renderer_branch_dlist[3]=0xBC001C04_0000FFFE
host_renderer_branch_dlist[4]=0xE7000000_00000000
host_renderer_branch_dlist[5]=0xFE000000_80142440
host_renderer_branch_dlist[6]=0xE7000000_00000000
host_renderer_branch_dlist[7]=0xB900031D_00000000
host_renderer_texture_image[0]=cmd=0x8011B488 w0=0xFDD00000 w1=0x58080000 resolved=0x58080000 valid=0 segmented=0 segment=0 segment_base=0x00000000
host_renderer_texture_image[1]=cmd=0x8011B880 w0=0xFDD00000 w1=0x8E020000 resolved=0x8E020000 valid=0 segmented=0 segment=0 segment_base=0x00000000
host_renderer_texture_image[2]=cmd=0x801001A0 w0=0xFD5FE0FF w1=0xFFC0EF07 resolved=0xFFC0EF07 valid=0 segmented=0 segment=0 segment_base=0x00000000
host_renderer_texture_image[3]=cmd=0x801001F0 w0=0xFDB83CCB w1=0x03822FC7 resolved=0x03822FC7 valid=0 segmented=1 segment=3 segment_base=0x00000000
host_renderer_texture_image[4]=cmd=0x80100260 w0=0xFD203D21 w1=0xBD08FD10 resolved=0xBD08FD10 valid=0 segmented=0 segment=0 segment_base=0x00000000
host_renderer_texture_image[5]=cmd=0x801002C8 w0=0xFD15FC15 w1=0x83E67DF6 resolved=0x83E67DF6 valid=0 segmented=0 segment=0 segment_base=0x00000000
host_renderer_texture_image[6]=cmd=0x80100350 w0=0xFDE1F913 w1=0x06283040 resolved=0x06283040 valid=0 segmented=1 segment=6 segment_base=0x00000000
host_renderer_texture_image[7]=cmd=0x80101050 w0=0xFD34BE74 w1=0x06CE2EDD resolved=0x06CE2EDD valid=0 segmented=1 segment=6 segment_base=0x00000000
host_rsp_task_done_queued count=1 queue=0x8005D9A0 msg=0x803B38EC type=2 queued=1 limit=1
host_rsp_task_done_delivered queue=0x8005D9A0 msg=0x803B38EC type=2 limit=1
host_rsp_task_consume_limit reached delivered=1; set GOLDENEYE_CONTINUE_AFTER_RSP_TASK=1 or GOLDENEYE_RSP_TASK_LIMIT=N to continue
entrypoint_probe=attempted child_exit=152
```

The produced local binary is ignored and not committed:

```text
ports/goldeneye/build-native-spike/goldeneye_native_spike
SHA256 8d5a357f581725f66fe2ceef05a6c9aa55aed16e1f423e331fd7fbab0aeaa348
```

This now proves:

1. generated GoldenEye translation units still compile and link into a native Linux x86-64 host executable;
2. the harness reserves a sparse host address space that matches N64Recomp low-address aliasing and maps the direct `0x80000400` section plus low-address `0x700...` / `0x7F...` sections into host memory;
3. the compressed cdata block is preloaded at `_csegmentSegmentStart`, allowing generated `init` to execute; the guarded child restores the local-only decomp ELF `.csegment` at the `initTLBPrepareContext` seam while generated inflate/TLB behavior remains incomplete;
4. first-pass ROM DMA, message queue, cooperative thread, VI framebuffer, and timing primitives execute in the host runtime;
5. guarded `recomp_entrypoint` dispatch is isolated in a child process and progresses through `recomp_entrypoint -> boot bridge -> generated init -> generated mainproc`, dispatching recorded thread id `3` past the debug registry and early audio/asset placeholders, through `guPerspectiveF`, through host frame ticks, and into a host renderer shim that recursively walks the first bounded display-list task, classifies presentation commands, prints a top-opcode histogram, validates backend resource addresses, emits named backend packet previews, previews texture image candidates with command addresses/raw words/segment state, and then delivers scheduler done messages back to `gfxFrameMsgQ`. Probe contexts initialize N64Recomp's odd-FPR pointer (`f_odd`) for MIPS3 float mode; without that, generated `guPerspectiveF` faulted while writing odd float registers.

It does **not** boot the game yet. The next blocker is real memp/resource provenance before RT64/custom command execution: the first bounded generated task now scans the top-level `303` commands, resolves all three segmented branch-display-list addresses, probes all three branch targets as `payload_or_unknown`, skips them by default, validates `10` of `10` backend address-bearing refs, classifies `2` malformed display-list image commands and `0` payload false positives in the guarded default path, and delivers the scheduler done message; a two-task guarded probe (`GOLDENEYE_RSP_TASK_LIMIT=2`) advances through two scheduler-done cycles with the same branch-guard/texture-classification shape. `GOLDENEYE_RENDERER_SCAN_PAYLOAD_BRANCHES=1` preserves the old forensic over-walk path and reproduces `raw_candidates=16 real_dl=2 malformed_dl=2 false_payload=12`. The runtime still needs real memp/resource provenance before F3DEX/RDP command mapping into RT64 or a custom renderer can produce meaningful output, plus stricter replacement of scheduler/video/audio/input placeholders.

