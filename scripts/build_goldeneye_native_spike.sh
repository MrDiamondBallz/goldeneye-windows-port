#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/ports/goldeneye/build-native-spike"
N64RECOMP_DIR="${N64RECOMP_DIR:-/root/projects/goldeneye-pc-port/N64Recomp}"

"$ROOT/scripts/run_goldeneye_n64recomp_spike.sh" /tmp/ge_native_spike_codegen.log

cmake -S "$ROOT/ports/goldeneye/app" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DN64RECOMP_DIR="$N64RECOMP_DIR"
cmake --build "$BUILD_DIR" --config RelWithDebInfo -j"$(nproc)"

"$BUILD_DIR/goldeneye_native_spike"
file "$BUILD_DIR/goldeneye_native_spike"
sha256sum "$BUILD_DIR/goldeneye_native_spike"
