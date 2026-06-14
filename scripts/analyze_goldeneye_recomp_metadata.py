#!/usr/bin/env python3
"""GoldenEye US metadata preflight for the N64Recomp native-port spike.

This does not emit or commit copyrighted game data. It inspects a local decomp ELF/map
and explains the metadata exceptions needed by the current N64Recomp codegen pass.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

EXPECTED_US_SHA1 = "abe01e4aeb033b6c0836819f549c791b26cfde83"

KNOWN_SYMBOLS = [
    "sizepropdef",
    "texInflateLookup",
    "boot",
    "resolve_TLBaddress_for_InvalidHit",
    "initTLBPrepareContext",
    "eqpower",
]

KNOWN_JUMP_TABLES = {
    "sizepropdef": (0x80053490, 0x7F056914),
    "texInflateLookup": (0x8005BDE8, 0x7F0CA900),
}

EXPECTED_IGNORES = {
    "sizepropdef": "temporary: previously exposed rodata-section jump-table inference issue; keep tracked until native runtime/function handling is verified",
    "boot": "hardware boot/PI/cop0 setup replaced by native runtime",
    "resolve_TLBaddress_for_InvalidHit": "TLB exception handler uses cop0 state; native runtime must replace/reimplement",
    "initTLBPrepareContext": "TLB/cop0 setup; native runtime must replace/reimplement",
    "eqpower": "ELF marks this as FUNC, but disassembly shows a packed lookup/data table",
}


@dataclass
class SymbolHit:
    name: str
    line: str | None
    nm_line: str | None


def sha1_file(path: Path) -> str:
    h = hashlib.sha1()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def run(cmd: list[str], cwd: Path | None = None) -> str:
    return subprocess.check_output(cmd, cwd=cwd, text=True, stderr=subprocess.STDOUT)


def find_map_line(map_text: str, needle: str) -> str | None:
    for line in map_text.splitlines():
        if needle in line:
            return line.strip()
    return None


def find_map_region_for_addr(map_text: str, addr: int) -> str | None:
    for line in map_text.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0] in {".text", ".rodata", ".data", ".bss"}:
            try:
                start = int(parts[1], 16)
                size = int(parts[2], 16)
            except ValueError:
                continue
            if start <= addr < start + size:
                return line.strip()
    return None


def parse_ignored(config_text: str) -> set[str]:
    m = re.search(r"ignored\s*=\s*\[(.*?)\]", config_text, flags=re.S)
    if not m:
        return set()
    body = re.sub(r"#.*", "", m.group(1))
    return set(re.findall(r'"([^"]+)"', body))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", type=Path, default=Path("/root/projects/goldeneye-windows-port"))
    ap.add_argument("--decomp", type=Path, default=Path("/root/projects/007"))
    ap.add_argument("--n64recomp", type=Path, default=Path("/root/projects/goldeneye-pc-port/N64Recomp"))
    args = ap.parse_args()

    rom = args.decomp / "baserom.u.z64"
    elf = args.decomp / "build/u/ge007.u.elf"
    map_file = args.decomp / "build/u/ge007.u.map"
    config = args.repo / "ports/goldeneye/config/ge007_us_recomp.toml"
    patch = args.repo / "patches/n64recomp/0001-resolve-jump-tables-from-rodata-section.patch"

    print("# GoldenEye US N64Recomp metadata preflight")
    print()

    for label, path in [("ROM", rom), ("ELF", elf), ("MAP", map_file), ("CONFIG", config), ("N64Recomp patch", patch)]:
        print(f"- {label}: {path} {'OK' if path.exists() else 'MISSING'}")
        if not path.exists():
            return 1

    actual_sha1 = sha1_file(rom)
    print(f"- ROM SHA1: {actual_sha1}")
    if actual_sha1 != EXPECTED_US_SHA1:
        print(f"ERROR: expected GoldenEye US SHA1 {EXPECTED_US_SHA1}")
        return 1

    map_text = map_file.read_text(errors="ignore")
    config_text = config.read_text()
    ignored = parse_ignored(config_text)

    print()
    print("## Known symbols")
    nm_text = run(["mips-linux-gnu-nm", "-n", str(elf)])
    ok = True
    for name in KNOWN_SYMBOLS:
        hit = SymbolHit(
            name=name,
            line=find_map_line(map_text, name),
            nm_line=next((ln for ln in nm_text.splitlines() if re.search(rf"\b{re.escape(name)}$", ln)), None),
        )
        print(f"- {name}:")
        print(f"  - map: {hit.line or 'MISSING'}")
        print(f"  - nm: {hit.nm_line or 'MISSING'}")
        if name in EXPECTED_IGNORES:
            print(f"  - ignored_reason: {EXPECTED_IGNORES[name]}")
            if name not in ignored:
                print("  - ERROR: expected in config ignored list")
                ok = False

    print()
    print("## Jump-table blockers resolved by N64Recomp patch")
    for func, (jtbl, instr) in KNOWN_JUMP_TABLES.items():
        rodata_line = (
            find_map_line(map_text, f"0x{jtbl:08x}")
            or find_map_line(map_text, f"0x{jtbl:08X}")
            or find_map_region_for_addr(map_text, jtbl)
        )
        print(f"- {func}: table=0x{jtbl:08X}, jr=0x{instr:08X}")
        print(f"  - map: {rodata_line or 'MISSING'}")
        if rodata_line is None:
            ok = False

    print()
    print("## Patch status")
    patch_status = subprocess.run(
        ["git", "-C", str(args.n64recomp), "apply", "--reverse", "--check", str(patch)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if patch_status.returncode == 0:
        print("- N64Recomp rodata jump-table patch: APPLIED")
    else:
        check_status = subprocess.run(
            ["git", "-C", str(args.n64recomp), "apply", "--check", str(patch)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if check_status.returncode == 0:
            print("- N64Recomp rodata jump-table patch: NOT APPLIED, but applies cleanly")
            ok = False
        else:
            print("- N64Recomp rodata jump-table patch: UNKNOWN/CONFLICT")
            print(check_status.stdout)
            ok = False

    print()
    print("RESULT:", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
