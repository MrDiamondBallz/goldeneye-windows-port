#!/usr/bin/env bash
set -euo pipefail

SERVER="${1:-/root/projects/goldeneye-pc-port/GoldenEye-Recomp-Server/build-linux/ge_server}"
BIND_IP="${2:-127.0.0.1}"
PORT="${3:-31000}"

timeout 2s "$SERVER" "$BIND_IP" "$PORT" || code=$?
if [[ "${code:-0}" == "124" ]]; then
  echo "Smoke OK: server stayed running until timeout."
  exit 0
fi
exit "${code:-0}"
