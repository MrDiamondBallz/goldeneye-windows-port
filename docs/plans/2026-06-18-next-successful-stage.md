# GoldenEye Native-Port Next Successful Stage — Optimized Plan

> **For Hermes:** Use `game-decomp-recomp-workflows` and `systematic-debugging` before implementing. If executing with subagents, use `subagent-driven-development` task-by-task. Keep all ROM/generated/proprietary outputs local and ignored.

**Goal:** Advance the GoldenEye 007 N64Recomp/native-port spike from a noisy renderer/asset boundary to a verified, explainable first-render-task boundary: display-list scanning must distinguish real render commands from payload/data false positives, and runtime memory resources must have provenance before we attempt real decompression or RT64/custom rendering.

**Architecture:** The old plan jumped too quickly to “asset decompression/memp bank modeling.” After probing the repo, the sharper path is: first fix observability and false-positive classification in the display-list scanner, then replace the simplistic host memp allocator with a real memp-pool mirror/provenance layer, then only debug decompression if real texture candidates remain unbacked. No full renderer until candidate/resource provenance is trustworthy.

**Tech Stack:** C++17 runtime harness, N64Recomp-generated GoldenEye code, CMake native spike, local user-owned GoldenEye USA ROM/ELF inputs, guarded child probes.

---

## Fresh probe receipts — 2026-06-18 PT

Repo:

```text
/root/projects/goldeneye-windows-port
branch: main...origin/main
latest commit: 5669323 Validate GoldenEye texture provenance
working tree: docs/plans/ untracked, generated/build dirs ignored
```

Build/probe still works:

```text
cmake --build ports/goldeneye/build-native-spike -j$(nproc) -> build_ok
normal probe -> controlled_probe_result=OK boot_primitives_enabled safe_generated_dispatch_enabled
```

Normal runtime state:

```text
sections: copied=4 skipped=1 copied_bytes=1068160 low_mapped=3 cdata_preloaded=78240 csegment_preloaded=0
runtime_primitives: rom_bytes=12582912 dma_copies=6 dma_bytes=1146464 queues_created=1 messages_sent=2 messages_received=1 threads_created=1 threads_started=1 threads_dispatched=0 rsp_tasks_started=0 rsp_done_messages_delivered=0
```

Guarded texture trace still reaches first RSP task safely:

```text
host_rsp_task_consume count=1 first_gdl=0x8011B320 end_gdl=0x8011BC98 flags=0x00000000 done_msg=0x803B38EC frame_ticks=2
host_renderer_presentation matrix=29 vertex=14 texture=11 triangles=10 geom_mode=21 tex_images=10 tex_malformed=6 tex_segmented=2 tex_resolved=0 tex_unresolved=10 color_images=20 depth_images=9 tile_setup=226 texture_loads=38 combine=14 sync=23 fill_rect=10 othermode=38 packets=58
host_renderer_backend packets=473 geometry=53 state=84 texture=274 target=39 sync=23 address_refs=82 valid_refs=25 invalid_refs=57
entrypoint_probe=attempted child_exit=152
```

Important new evidence from `GOLDENEYE_RENDERER_TRACE_TEXTURES=1`:

```text
host_renderer_texture_trace cmd=0x8011B488 w0=0xFDD00000 w1=0x58080000 ... nearby BB/BA/FD/F5/E6
host_renderer_texture_trace cmd=0x8011B880 w0=0xFDD00000 w1=0x8E020000 ... nearby BB/BA/FD/F5/E6
host_renderer_texture_trace cmd=0x801001A0 w0=0xFD5FE0FF w1=0xFFC0EF07 ... nearby random-looking opcodes
host_renderer_texture_trace cmd=0x801001F0 w0=0xFDB83CCB w1=0x03822FC7 ... nearby random-looking opcodes
...
```

Interpretation:

- The first two `FDD00000` candidates are inside plausible RDP command neighborhoods but are likely malformed image commands, not backed texture images.
- Most `tex_images=10` candidates come from `0x80100xxx`/`0x80101xxx` regions with random-looking nearby opcodes, likely scanner over-walk into payload/decompressed data or bad branch targets.
- Therefore, the next stage should **first make the renderer scanner prove which candidates are real display-list commands** before spending the day on decompression.

Second important evidence: current memp replacement is too fake/noisy:

```text
mempAllocBytesInBank host allocator failed bytes=0x0 bank=6 cursor=0x80100000
mempAllocBytesInBank host allocator failed bytes=0xB00400 bank=4 cursor=0x801188A0
mempAllocBytesInBank host allocator failed bytes=0xD0DD0600 bank=4 cursor=0x8011B720
```

Relevant decomp facts:

```text
memp.h: void *mempAllocBytesInBank(u32 bytes, u8 bank);
memp.h: s32 mempAddEntryOfSizeToBank(u8* ptrdata, u32 size, u8 bank);
g_mempPools address from generated asm: 0x80063BB0
needmemallocation address from generated asm: 0x80024404
```

Interpretation:

- The function signature is correct: `a0=size`, `a1=bank`.
- The host replacement is wrong architecturally because it ignores GoldenEye’s real `g_mempPools` state and uses one global bump cursor.
- Bad allocations may be downstream symptoms of earlier placeholders, but a real memp mirror/provenance layer is still necessary before asset/render work.

---

## Definition of next successful stage

A good next-stage checkpoint today is:

1. **Scanner truth:** first RSP-task texture candidates are classified as `real_dl_texture`, `malformed_dl_image`, or `false_positive_payload`, with branch/list provenance.
2. **Reduced noise:** random `0x80100xxx` payload scans no longer count as real texture image commands, or they are explicitly labeled false positives with branch path/source.
3. **Memp truth:** memp allocations are modeled against GoldenEye’s real `g_mempPools` layout or, at minimum, recorded with bank/size/return/provenance and invalid reasons.
4. **Resource truth:** ROM DMA/cdata/csegment/memp/decompress records can answer “is this address backed and where did it come from?”
5. **Safety preserved:** normal probe passes; guarded one-task/two-task probes exit through the existing RSP limit, not crash/hang.
6. **Legal hygiene:** no ROMs, generated code, binaries, extracted assets, or credentials are staged.

Stretch win:

```text
tex_false_positive_payload > 0
tex_real_unbacked reason=<specific>
```

Bigger stretch:

```text
tex_backed >= 1
```

Do not require `tex_backed>=1` if the scanner proves current “valid texture” candidates are false positives.

---

## Optimized implementation sequence

### Task 0: Preserve baseline and avoid dirty confusion

**Objective:** Start from a known tree and keep the plan file separate from code changes.

**Files:**
- Existing untracked plan: `docs/plans/2026-06-18-next-successful-stage.md`

**Commands:**

```bash
cd /root/projects/goldeneye-windows-port
git status -sb --ignored
git log --oneline -5
```

**Expected:** only `docs/plans/` untracked plus ignored `ports/goldeneye/build-native-spike/` and `ports/goldeneye/generated/` before code changes.

---

### Task 1: Add renderer branch/list provenance instrumentation

**Objective:** Make every texture candidate explain where it came from: top-level list, branch target, recursion depth, branch source command, and whether the surrounding command stream looks like a display list.

**Files:**
- Modify: `ports/goldeneye/runtime/renderer.h`
- Modify: `ports/goldeneye/runtime/renderer.cpp`

**Implementation direction:**

Extend `GoldenEyeRendererTextureImagePreview` with:

```cpp
uint32_t list_start{};
uint32_t branch_source_addr{};
uint32_t depth{};
uint32_t local_command_index{};
bool plausible_command_neighborhood{};
bool likely_payload_false_positive{};
```

Thread list provenance through:

```cpp
scan_display_list(... list_start, branch_source_addr, depth, ...)
classify_command(... command_addr, list_start, branch_source_addr, depth, local_index, ...)
```

Add a tiny local plausibility helper:

```cpp
bool looks_like_display_list_neighborhood(uint8_t* rdram, uint32_t command_addr);
```

Initial heuristic only; do not overfit:

- Valid/plausible if nearby opcodes include normal RDP texture sequence (`0xFD/0xF5/0xE6/0xF3/0xF4/0xF2`) or geometry sequence (`0x01/0x04/0xBF/0xB7/0xB8`).
- Suspicious if ±2 commands are mostly random non-GBI/RDP opcodes and there is no `EndDL` within a bounded scan.

**Verification:**

```bash
cmake --build ports/goldeneye/build-native-spike -j"$(nproc)"
GOLDENEYE_TRY_ENTRYPOINT=1 GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=25 GOLDENEYE_RSP_TASK_LIMIT=1 GOLDENEYE_RENDERER_TRACE_TEXTURES=1 ports/goldeneye/build-native-spike/goldeneye_native_spike | tee /tmp/ge-task1-renderer-provenance.log
grep -E 'host_renderer_texture_image|host_renderer_texture_trace|host_renderer_presentation' /tmp/ge-task1-renderer-provenance.log
```

**Expected:** the `0x80100xxx` candidates are visibly traceable to a branch/list path and plausibility label.

---

### Task 2: Separate malformed images, real texture images, and payload false positives

**Objective:** Stop treating payload bytes as equal to display-list texture commands.

**Files:**
- Modify: `ports/goldeneye/runtime/renderer.h`
- Modify: `ports/goldeneye/runtime/renderer.cpp`

**Implementation direction:**

Add counters:

```cpp
uint32_t texture_image_real_dl_commands{};
uint32_t texture_image_payload_false_positives{};
uint32_t texture_image_malformed_dl_commands{};
uint32_t texture_image_real_unbacked{};
```

Classification rule:

1. Existing `!is_valid_set_image_command(w0)` -> `malformed_dl_command` only if neighborhood/list is plausible; otherwise payload false positive.
2. Existing valid-format `0xFD` in implausible/random neighborhood -> `payload_false_positive`.
3. Existing valid-format `0xFD` in plausible DL neighborhood -> real texture candidate, then resolve/backing check.

Keep old totals for comparison, but add a new line:

```text
host_renderer_texture_classification real_dl=N malformed_dl=N false_payload=N real_unbacked=N backed=N
```

**Verification:**

```bash
cmake --build ports/goldeneye/build-native-spike -j"$(nproc)"
GOLDENEYE_TRY_ENTRYPOINT=1 GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=25 GOLDENEYE_RSP_TASK_LIMIT=1 GOLDENEYE_RENDERER_TRACE_TEXTURES=1 ports/goldeneye/build-native-spike/goldeneye_native_spike | tee /tmp/ge-task2-texture-classification.log
grep -E 'texture_classification|host_renderer_texture_image|host_renderer_presentation' /tmp/ge-task2-texture-classification.log
```

**Expected:** `0x801001A0`, `0x801001F0`, `0x80100260`, etc. should become `false_payload` unless instrumentation proves a legitimate display-list path.

**Decision gate:**

- If most/only “valid” textures are false positives, next work is branch/list boundary accuracy, not decompression.
- If real DL texture candidates remain, continue to resource provenance.

---

### Task 3: Add optional branch-target plausibility guard

**Objective:** Prevent recursive scanner walks from wandering into payload/data regions while preserving debugging visibility.

**Files:**
- Modify: `ports/goldeneye/runtime/renderer.cpp`

**Implementation direction:**

Before pushing a `BranchTarget`, inspect the first 3–8 commands at target:

```cpp
BranchPlausibility classify_branch_target(uint8_t* rdram, uint32_t target, const segments&);
```

Classify:

- `plausible_display_list`
- `plausible_but_malformed`
- `payload_or_unknown`
- `untranslated`

Default behavior after this task:

- Still count/report payload branch targets.
- Do not recursively scan `payload_or_unknown` targets as display lists unless `GOLDENEYE_RENDERER_SCAN_PAYLOAD_BRANCHES=1` is set.

Print:

```text
host_renderer_branch_target source=0x... target=0x... depth=... classification=payload_or_unknown first_ops=...
```

**Verification:**

```bash
cmake --build ports/goldeneye/build-native-spike -j"$(nproc)"
GOLDENEYE_TRY_ENTRYPOINT=1 GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=25 GOLDENEYE_RSP_TASK_LIMIT=1 ports/goldeneye/build-native-spike/goldeneye_native_spike | tee /tmp/ge-task3-branch-guard.log
grep -E 'branch_target|texture_classification|host_renderer_execute|host_renderer_presentation' /tmp/ge-task3-branch-guard.log
```

**Expected:** command count/noise drops if false branches were being followed; safe exit remains `child_exit=152`.

---

### Task 4: Replace fake global memp cursor with real memp pool mirror

**Objective:** Make `mempAllocBytesInBank` and `mempAddEntryOfSizeToBank` behave like GoldenEye’s bank allocator and provide useful provenance.

**Files:**
- Modify: `ports/goldeneye/runtime/runtime.h`
- Modify: `ports/goldeneye/runtime/runtime.cpp`
- Modify: `ports/goldeneye/runtime/stubs.cpp`

**Facts to encode:**

```text
g_mempPools = 0x80063BB0
needmemallocation = 0x80024404
memp pool struct stride = 0x10
memp pool fields from asm/use: start=+0x00, pos=+0x04, end=+0x08, prevpos=+0x0C
mempAllocBytesInBank(a0=bytes, a1=bank)
mempAddEntryOfSizeToBank(a0=ptr, a1=newsize, a2=bank)
```

Implementation sketch:

```cpp
struct RuntimeMempPool { uint32_t start; uint32_t pos; uint32_t end; uint32_t prevpos; };
bool read_memp_pool(uint8_t* rdram, uint8_t bank, RuntimeMempPool* out);
bool write_memp_pool(uint8_t* rdram, uint8_t bank, const RuntimeMempPool& pool);
```

`mempAllocBytesInBank` should:

1. Read requested bank pool.
2. If uninitialized (`pos==0`), log structured failure and return 0.
3. If allocation exceeds bank, attempt bank 6 like original.
4. Set `prevpos=old_pos`, `pos=aligned/new_pos`, return old_pos.
5. Record provenance: `kind=memp`, `bank`, `vaddr`, `size`, `source=alloc`.

`mempAddEntryOfSizeToBank` should:

1. Check `needmemallocation` and fallback to bank 6 if ptr equals bank 6 prevpos.
2. Verify ptr equals `pool.prevpos`.
3. Resize `pool.pos` based on `newsize`.
4. Record provenance update: `kind=memp_resize`.

**Verification:**

```bash
cmake --build ports/goldeneye/build-native-spike -j"$(nproc)"
GOLDENEYE_TRY_ENTRYPOINT=1 GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=25 GOLDENEYE_RSP_TASK_LIMIT=1 ports/goldeneye/build-native-spike/goldeneye_native_spike | tee /tmp/ge-task4-memp.log
grep -E 'memp_|resource_provenance|runtime_primitives|entrypoint_probe' /tmp/ge-task4-memp.log
```

**Expected:** fewer nonsensical global-cursor allocation failures; structured memp lines show bank start/pos/end/prevpos and real allocation/resize reasons.

**Decision gate:**

- If memp pools are uninitialized before use, trace whether generated `mempCheckMemflagTokens`/`mempSetBankStarts` ran and what total pool start/size were.
- Do not paper over this with a bigger cursor; fix or model pool initialization.

---

### Task 5: Add resource provenance table and renderer backed-resource checks

**Objective:** Let the renderer ask “is this address backed by ROM DMA/cdata/csegment/memp/decompress?” rather than only “does host memory translate?”

**Files:**
- Modify: `ports/goldeneye/runtime/runtime.h`
- Modify: `ports/goldeneye/runtime/runtime.cpp`
- Modify: `ports/goldeneye/runtime/renderer.h`
- Modify: `ports/goldeneye/runtime/renderer.cpp`
- Modify: `ports/goldeneye/runtime/stubs.cpp`
- Modify: `ports/goldeneye/runtime/libultra_stubs.cpp`

**Implementation direction:**

Add:

```cpp
enum class GoldenEyeResourceKind { RomDma, CDataPreload, CSegmentElfRestore, MempAlloc, MempResize, DecompressStub, Unknown };
struct GoldenEyeResourceProvenance {
    uint32_t vaddr;
    uint32_t size;
    uint32_t source_rom;
    uint8_t bank;
    GoldenEyeResourceKind kind;
};
```

Helpers:

```cpp
void goldeneye_runtime_record_resource(...);
bool goldeneye_runtime_find_resource(uint32_t vaddr, GoldenEyeResourceProvenance* out);
void goldeneye_runtime_print_resource_summary();
```

Record resources from:

- `goldeneye_runtime_copy_rom_to_vaddr` / `osPiStartDma_recomp`
- cdata preload
- csegment ELF restore in `initTLBPrepareContext`
- memp alloc/resize
- `decompressdata` stub/bridge attempts

Renderer adds:

```text
host_renderer_texture_resource cmd=0x... addr=0x... backed=0/1 kind=... bank=... size=... reason=...
```

**Verification:**

```bash
cmake --build ports/goldeneye/build-native-spike -j"$(nproc)"
GOLDENEYE_TRY_ENTRYPOINT=1 GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=25 GOLDENEYE_RSP_TASK_LIMIT=1 GOLDENEYE_RENDERER_TRACE_TEXTURES=1 ports/goldeneye/build-native-spike/goldeneye_native_spike | tee /tmp/ge-task5-resource-provenance.log
grep -E 'resource_summary|texture_resource|texture_classification|host_renderer_presentation' /tmp/ge-task5-resource-provenance.log
```

**Expected:** render targets/matrices/texture candidates can distinguish “translated memory” from “known resource-backed memory.”

---

### Task 6: Only then decide whether to debug decompression

**Objective:** Avoid burning time in generated zlib until evidence says real texture candidates depend on it.

**Files:**
- Modify only if needed: `ports/goldeneye/runtime/stubs.cpp`
- Maybe inspect generated/local-only: `ports/goldeneye/generated/us_recomp/*`

**Decision tree:**

1. If all current texture candidates were false positives: skip decompression today; checkpoint scanner truth + memp/resource provenance.
2. If real DL texture candidates point into ROM DMA/cdata/memp already: map/copy/record those resources first.
3. If real DL texture candidates require `decompressdata`: run opt-in bridge with better diagnostics:

```bash
GOLDENEYE_ENABLE_DECOMPRESS_BRIDGE=1 \
GOLDENEYE_TRY_ENTRYPOINT=1 \
GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=25 \
ports/goldeneye/build-native-spike/goldeneye_native_spike | tee /tmp/ge-decompress-bridge.log
```

Add diagnostics around:

```text
source, target, hlist, rz_inbuf, rz_outbuf, rz_wp, child exit/signal
```

Do **not** remove `decompressdata` from ignored replacements globally unless a small bounded proof works.

---

### Task 7: Final verification matrix

**Objective:** Prove the checkpoint is better and still safe.

**Commands:**

```bash
cd /root/projects/goldeneye-windows-port
cmake --build ports/goldeneye/build-native-spike -j"$(nproc)"
ports/goldeneye/build-native-spike/goldeneye_native_spike | tee /tmp/ge-final-normal.log
GOLDENEYE_TRY_ENTRYPOINT=1 GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=25 GOLDENEYE_RSP_TASK_LIMIT=1 ports/goldeneye/build-native-spike/goldeneye_native_spike | tee /tmp/ge-final-rsp1.log
GOLDENEYE_TRY_ENTRYPOINT=1 GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=30 GOLDENEYE_RSP_TASK_LIMIT=2 ports/goldeneye/build-native-spike/goldeneye_native_spike | tee /tmp/ge-final-rsp2.log
GOLDENEYE_TRY_ENTRYPOINT=1 GOLDENEYE_ENTRYPOINT_TIMEOUT_SEC=25 GOLDENEYE_RSP_TASK_LIMIT=1 GOLDENEYE_RENDERER_TRACE_TEXTURES=1 ports/goldeneye/build-native-spike/goldeneye_native_spike | tee /tmp/ge-final-trace.log
```

Expected:

```text
controlled_probe_result=OK
entrypoint_probe=attempted child_exit=152
host_renderer_texture_classification ...
resource_summary ...
```

Regression fails if:

- normal probe no longer reaches `controlled_probe_result=OK`;
- guarded probe crashes/hangs without diagnostic child exit;
- command/list scan explodes or loses scheduler done-message delivery;
- public commit would stage assets/generated output.

---

### Task 8: Docs, checkpoint, hygiene, commit

**Objective:** Save the day’s progress in a resumable, publish-safe checkpoint.

**Files:**
- Modify: `README.md`
- Modify: `docs/N64_ROM_NATIVE_PORT_ASSIGNMENT.md`
- Modify: `docs/plans/2026-06-18-next-successful-stage.md` if implementation changes the plan
- Patch skill checkpoint after success: `/root/.hermes/skills/gaming/game-decomp-recomp-workflows/references/goldeneye-renderer-task-scanner-checkpoint.md`

**Hygiene commands:**

```bash
git status --short --ignored
git diff --check
find . -type f \( -name '*.z64' -o -name '*.n64' -o -name '*.v64' -o -name '*.rom' -o -name '*.iso' -o -name '*.xex' \) -not -path './.git/*' -print
git diff --cached --name-only
```

Commit only after proof:

```bash
git add README.md docs/ ports/goldeneye/runtime/ ports/goldeneye/app/ ports/goldeneye/config/ scripts/
git commit -m "Clarify GoldenEye renderer resource provenance"
git push origin main
```

Use MrDiamondBallz no-reply metadata and verify GitHub API after push if publishing.

---

## Recommended first execution step when Tristan says “Go”

Start with **Task 1 + Task 2 only**:

1. Add renderer branch/list provenance instrumentation.
2. Split texture classifications into malformed DL vs real DL vs payload false-positive.
3. Re-run the exact guarded texture trace.

Why this first: the live probe showed many “texture” candidates are probably scanner overreach into payload/data. If we fix memp/decompression first, we may spend hours backing fake addresses. Scanner truth is the multiplier.

## Non-goals for this stage

- Claiming the game boots.
- Full RT64 integration.
- Full decompressor rewrite.
- Audio backend.
- Online multiplayer/server.
- Controller UX.
- Shipping a Windows `.exe`.

Those come after the first renderer task has trustworthy command/resource provenance.
