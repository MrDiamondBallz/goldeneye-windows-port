# GoldenEye Windows Native PC Port Plan

> **Goal:** Build a real Windows `.exe` PC port of GoldenEye with controller support, host/join online play, and a dedicated/invite-friendly server flow — no emulator UI, no RetroArch, no Project64.

## Ground truth from reconnaissance

### What we already have locally

- N64 decomp repo: `/root/projects/007`
- Verified matching N64 ROM build: `/root/projects/007/build/u/ge007.u.z64`
- GoldenEye PC recomp repos cloned under: `/root/projects/goldeneye-pc-port/`
  - `GoldenEye-Recomp` — app/window/menu/hooks wrapper for XBLA GoldenEye ReXGlue recomp
  - `GoldenEye-Recomp-Server` — standalone matchmaker + UDP relay
  - `GoldenEye-Recomp-rexglue` — modified ReXGlue SDK with GoldenEye-specific fixes/online seams
  - `N64Recomp` — N64 static recompilation toolchain
  - `RecompFrontend` — SDL2 controller/input + UI library for N64 recomp projects

### Important legal/project boundary

We can build tooling and wrappers, and we can commit public-domain / explicitly redistributable ROM fixtures with source/license notes. We cannot fetch, share, commit, or package commercial copyrighted game data. Any commercial game code/assets must come from legally owned local files and stay outside public git history.

## Architecture decision

There are two viable routes, and they are not equivalent.

| Route | Input asset | Output | Online feasibility | Controller support | Time/risk |
|---|---|---|---|---|---|
| **A. ReXGlue/XBLA route** | `assets/default.xex` from GoldenEye 007 Xbox 360/XBLA build | Windows `ge.exe` | **Strong** — XBLA already has System Link; existing server relays it | Existing port includes controller support | Fastest / lowest risk |
| **B. N64Recomp route** | `baserom.u.z64` / N64 binary + symbols | Windows native `.exe` | **Hard** — N64 GoldenEye has only local split-screen, no LAN protocol; must build lockstep/input sync ourselves | RecompFrontend SDL2 input available | Longer / high risk |

**Recommendation:** If the requirement is “Windows `.exe` + controller + online host/join like ReXGlue,” start with **Route A**. It already matches the exact feature shape. The blocker is not engineering — it is needing the user's own XBLA GoldenEye game file at `assets/default.xex`.

If the requirement is specifically “use the N64 ROM/decomp we uploaded,” then we do **Route B**, but online play becomes a custom netcode project rather than a simple relay.

## Route A — ReXGlue/XBLA implementation plan

### Target UX

- `GoldenEye.exe` launches natively on Windows.
- Main menu / pause menu has:
  - Controller settings
  - Keyboard/mouse settings if wanted later
  - Video/fullscreen/resolution/FPS settings
  - Online menu: username, server address, port, enable online
- `Host Server` button or bundled `ge_server.exe` lets the player host.
- Friend connects by address/port, using Hamachi/playit.gg/VPS/port-forward.

### Required local files

Place legally owned XBLA game file here:

```text
/root/projects/goldeneye-pc-port/GoldenEye-Recomp/assets/default.xex
```

`ge_manifest.toml` currently expects exactly:

```toml
[entrypoint]
file_path = "assets/default.xex"
out_directory_path = "generated"
```

### Build steps once `default.xex` exists

1. Build/prepare ReXGlue SDK from:

```text
/root/projects/goldeneye-pc-port/GoldenEye-Recomp-rexglue
```

2. Generate recompiled code:

```bash
cd /root/projects/goldeneye-pc-port/GoldenEye-Recomp
rexglue codegen --max_jump_table_entries 2048 ge_config.toml
```

Expected output:

```text
generated/rexglue.cmake
generated/... recompiled C++ sources ...
```

3. Configure game build:

```bash
cmake --preset win-amd64-relwithdebinfo -DREXSDK_DIR=/root/projects/goldeneye-pc-port/GoldenEye-Recomp-rexglue
```

Note: this repo currently lists Linux presets locally; Windows build should be done on a Windows/MSVC machine/MSVC if the preset is absent or repo needs a Windows preset added.

4. Build Windows executable:

```bash
cmake --build --preset win-amd64-relwithdebinfo
```

Expected output:

```text
ge.exe
```

5. Package:

```text
GoldenEye-PC-Port/
  ge.exe
  ge_server.exe
  assets/default.xex        # user-owned, never committed
  README-FIRST-RUN.md
```

### Already completed artifact: server

Built from `SunJaycy/GoldenEye-Recomp-Server`:

```text
Linux server:   /root/projects/goldeneye-pc-port/GoldenEye-Recomp-Server/build-linux/ge_server
Windows server: /root/projects/goldeneye-pc-port/GoldenEye-Recomp-Server/build-win64/ge_server.exe
```

Windows server hash:

```text
SHA256 7d55496cfa79f82ff427fb6f3b0a3bfb6aa2d23d003590d0a8f3e11dc773f504
```

Smoke test passed on Linux:

```text
[ge-server] GoldenEye matchmaker+relay listening on 127.0.0.1:31000
```

## Route B — N64Recomp implementation plan

Use this only if the port must be based on the uploaded N64 ROM/decomp.

### Architecture

- Recompile N64 code with `N64Recomp`.
- Use an RT64/N64ModernRuntime-style runtime for graphics/audio/platform.
- Use `RecompFrontend` for SDL2 controller, keyboard, mouse, menus, config profiles.
- Build game-specific glue for:
  - ROM/RDRAM initialization
  - overlays/segments
  - controller pak/save data
  - frame pacing
  - widescreen/HD/render options
  - local multiplayer input mapping

### Online problem

N64 GoldenEye multiplayer is local split-screen. It does not have System Link. A relay server is not enough because there is no native network protocol to relay.

Online options:

1. **Deterministic lockstep remote input**
   - Each client runs the full game.
   - Server distributes controller inputs per frame.
   - Requires strict determinism, latency buffering, desync detection/resync.
   - Best for “classic split-screen but online.”

2. **Host-authoritative gameplay sync**
   - Host runs truth simulation.
   - Clients receive entity/player/world snapshots.
   - Requires deep game-specific hooks and more custom engine work.
   - More modern feel, much harder.

3. **XBLA/ReXGlue System Link relay**
   - Already exists in Route A.
   - Not applicable to N64 unless we implement a network protocol ourselves.

### First N64 spikes

1. **N64Recomp codegen spike**
   - Given `build/u/ge007.u.z64` and symbol/config data,
   - when N64Recomp runs,
   - then it emits compilable C/C++ for at least the boot/static code path.

2. **Windows SDL2 controller shell spike**
   - Given a minimal C++ app,
   - when an Xbox/PlayStation controller is connected,
   - then SDL2 maps buttons/sticks into N64 controller inputs and persists profiles.

3. **Runtime boot spike**
   - Given generated code + runtime glue,
   - when `GoldenEyeN64Recomp.exe` launches,
   - then it reaches the first visible frame or debug heartbeat.

4. **Online lockstep spike**
   - Given two local clients + UDP server,
   - when controller input is exchanged for 300 frames,
   - then both clients agree on frame counters/state hash.

## Immediate next action

For the fastest path to the requested feature set, provide your legally owned XBLA `default.xex`, then run Route A. Without that, we can keep building infrastructure, UI/launcher/server packaging, and N64 route spikes, but we cannot produce the final playable `ge.exe`.
