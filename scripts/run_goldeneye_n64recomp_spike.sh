#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
N64RECOMP_DIR="${N64RECOMP_DIR:-/root/projects/goldeneye-pc-port/N64Recomp}"
N64RECOMP="$N64RECOMP_DIR/build/N64Recomp"
CONFIG="$ROOT/ports/goldeneye/config/ge007_us_recomp.toml"
OUT="$ROOT/ports/goldeneye/generated/us_recomp"
LOG="${1:-/tmp/ge_n64recomp_spike.log}"

"$ROOT/scripts/apply_n64recomp_patches.sh"
cmake --build "$N64RECOMP_DIR/build" --config Release -j"$(nproc)"

if [[ ! -x "$N64RECOMP" ]]; then
  echo "Missing N64Recomp binary: $N64RECOMP" >&2
  exit 1
fi

if [[ ! -f /root/projects/007/build/u/ge007.u.elf ]]; then
  echo "Missing GoldenEye decomp ELF: /root/projects/007/build/u/ge007.u.elf" >&2
  exit 1
fi

if [[ ! -f /root/projects/007/baserom.u.z64 ]]; then
  echo "Missing verified GoldenEye ROM copy: /root/projects/007/baserom.u.z64" >&2
  exit 1
fi

rm -rf "$OUT"
mkdir -p "$(dirname "$OUT")"

set +e
(
  cd "$ROOT"
  "$N64RECOMP" "$CONFIG"
) > "$LOG" 2>&1
code=$?
set -e

cat "$LOG"
echo
python3 - <<PY
from pathlib import Path
root = Path('$OUT')
print('Generated summary:')
print('exists', root.exists())
if root.exists():
    files = [p for p in root.rglob('*') if p.is_file()]
    print('file_count', len(files))
    print('total_bytes', sum(p.stat().st_size for p in files))
    for p in files[:20]:
        print(p)
PY

exit "$code"
