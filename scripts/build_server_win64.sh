#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-/root/projects/goldeneye-pc-port/GoldenEye-Recomp-Server}"
BUILD="$ROOT/build-win64"

if ! command -v x86_64-w64-mingw32-g++ >/dev/null; then
  echo "Missing x86_64-w64-mingw32-g++; install g++-mingw-w64-x86-64-posix" >&2
  exit 1
fi

cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
cmake --build "$BUILD" --config Release -j"$(nproc)"
file "$BUILD/ge_server.exe"
sha256sum "$BUILD/ge_server.exe"
