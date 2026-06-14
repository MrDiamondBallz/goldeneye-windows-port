#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
N64RECOMP_DIR="${N64RECOMP_DIR:-/root/projects/goldeneye-pc-port/N64Recomp}"
PATCH_FILE="$ROOT/patches/n64recomp/0001-resolve-jump-tables-from-rodata-section.patch"

if [[ ! -d "$N64RECOMP_DIR/.git" ]]; then
  echo "Missing N64Recomp checkout: $N64RECOMP_DIR" >&2
  echo "Set N64RECOMP_DIR=/path/to/N64Recomp if needed." >&2
  exit 1
fi

cd "$N64RECOMP_DIR"

if git apply --check "$PATCH_FILE" >/dev/null 2>&1; then
  git apply "$PATCH_FILE"
  echo "Applied N64Recomp rodata jump-table patch."
elif git apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "N64Recomp rodata jump-table patch already applied."
else
  echo "Patch does not apply cleanly and is not already applied:" >&2
  echo "$PATCH_FILE" >&2
  git diff -- src/analysis.cpp >&2 || true
  exit 1
fi
