#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
PORT="${1:-8091}"
MACHINE="demo-machine"
CACHE_FILE="$ROOT_DIR/license_cache.txt"
SERVER_LOG="/tmp/duc_server_demo.log"

mkdir -p "$BUILD_DIR"

g++ -std=c++17 -O2 -I"$ROOT_DIR/include" \
  "$ROOT_DIR/src/common.cpp" "$ROOT_DIR/src/http.cpp" "$ROOT_DIR/src/log.cpp" "$ROOT_DIR/src/license_server_main.cpp" \
  -o "$BUILD_DIR/license_server"

g++ -std=c++17 -O2 -I"$ROOT_DIR/include" \
  "$ROOT_DIR/src/common.cpp" "$ROOT_DIR/src/http.cpp" "$ROOT_DIR/src/log.cpp" "$ROOT_DIR/src/license_client_main.cpp" \
  -o "$BUILD_DIR/license_client"

rm -f "$CACHE_FILE" "$SERVER_LOG"
"$BUILD_DIR/license_server" --port "$PORT" --duration 40 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" >/dev/null 2>&1 || true' EXIT

sleep 1

echo "== Step 1: Activate =="
"$BUILD_DIR/license_client" activate --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE"

echo "== Step 2: Run online =="
"$BUILD_DIR/license_client" run --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE" --grace 5

echo "== Step 3: Stop server and run within grace =="
kill "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null || true
sleep 1
"$BUILD_DIR/license_client" run --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE" --grace 5

echo "== Step 4: Wait > grace and run again (should deny) =="
sleep 6
set +e
"$BUILD_DIR/license_client" run --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE" --grace 5
EXIT_CODE=$?
set -e

echo "Final exit code (expect non-zero): $EXIT_CODE"

echo "== Demo complete =="
echo "Cache file: $CACHE_FILE"
echo "Server log : $SERVER_LOG"
