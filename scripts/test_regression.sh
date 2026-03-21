#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
ARTIFACT_DIR="$ROOT_DIR/test_artifacts"
REPORT_FILE="$ARTIFACT_DIR/latest_test_report.txt"
SERVER_LOG="$ARTIFACT_DIR/test_server.log"
CACHE_FILE="$ARTIFACT_DIR/license_cache_test.txt"
PORT="${1:-18091}"
MACHINE="qa-node-01"

PASS_COUNT=0
FAIL_COUNT=0

mkdir -p "$BUILD_DIR" "$ARTIFACT_DIR"
: > "$REPORT_FILE"
: > "$SERVER_LOG"

SERVER_PID=""

log() {
  echo "$*" | tee -a "$REPORT_FILE"
}

pass() {
  PASS_COUNT=$((PASS_COUNT + 1))
  log "[PASS] $1"
}

fail() {
  FAIL_COUNT=$((FAIL_COUNT + 1))
  log "[FAIL] $1"
}

stop_server() {
  if [[ -n "${SERVER_PID}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  SERVER_PID=""
}

start_server() {
  local duration="$1"
  stop_server
  "$BUILD_DIR/license_server" --port "$PORT" --duration "$duration" > "$SERVER_LOG" 2>&1 &
  SERVER_PID=$!
  sleep 1
}

run_cmd() {
  set +e
  "$@"
  local rc=$?
  set -e
  return "$rc"
}

assert_success_contains() {
  local case_id="$1"
  local expected="$2"
  shift 2

  local out
  set +e
  out=$("$@" 2>&1)
  local rc=$?
  set -e

  if [[ "$rc" -eq 0 && "$out" == *"$expected"* ]]; then
    pass "$case_id"
  else
    fail "$case_id"
    log "  command: $*"
    log "  expected success and output contains: $expected"
    log "  rc=$rc"
    log "  out=$out"
  fi
}

assert_fail_contains() {
  local case_id="$1"
  local expected="$2"
  shift 2

  local out
  set +e
  out=$("$@" 2>&1)
  local rc=$?
  set -e

  if [[ "$rc" -ne 0 && "$out" == *"$expected"* ]]; then
    pass "$case_id"
  else
    fail "$case_id"
    log "  command: $*"
    log "  expected failure and output contains: $expected"
    log "  rc=$rc"
    log "  out=$out"
  fi
}

build_binaries() {
  log "[INFO] Building binaries"
  g++ -std=c++17 -O2 -I"$ROOT_DIR/include" \
    "$ROOT_DIR/src/common.cpp" "$ROOT_DIR/src/http.cpp" "$ROOT_DIR/src/log.cpp" "$ROOT_DIR/src/license_server_main.cpp" \
    -o "$BUILD_DIR/license_server"

  g++ -std=c++17 -O2 -I"$ROOT_DIR/include" \
    "$ROOT_DIR/src/common.cpp" "$ROOT_DIR/src/http.cpp" "$ROOT_DIR/src/log.cpp" "$ROOT_DIR/src/license_client_main.cpp" \
    -o "$BUILD_DIR/license_client"

  g++ -std=c++17 -O2 -I"$ROOT_DIR/include" \
    "$ROOT_DIR/src/common.cpp" "$ROOT_DIR/src/http.cpp" "$ROOT_DIR/src/log.cpp" "$ROOT_DIR/tests/test_duccore.cpp" \
    -o "$BUILD_DIR/duccore_tests"
}

cleanup() {
  stop_server
  rm -f "$CACHE_FILE"
}

trap cleanup EXIT

log "[INFO] Regression started at $(date '+%Y-%m-%d %H:%M:%S')"
log "[INFO] Root: $ROOT_DIR"
build_binaries
assert_success_contains "TC00 unit tests" "[SUMMARY] pass=" "$BUILD_DIR/duccore_tests"

# TC01-TC04: activation + online + offline grace
rm -f "$CACHE_FILE"
start_server 40

assert_success_contains "TC01 activate online" "[activate] success" \
  "$BUILD_DIR/license_client" activate --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE"

assert_success_contains "TC02 run online" "allowed (online heartbeat ok)" \
  "$BUILD_DIR/license_client" run --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE" --grace 5

stop_server
assert_success_contains "TC03 run offline within grace" "allowed (offline grace mode)" \
  "$BUILD_DIR/license_client" run --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE" --grace 5

sleep 6
assert_fail_contains "TC04 run offline beyond grace" "grace exceeded" \
  "$BUILD_DIR/license_client" run --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE" --grace 5

# TC05: expired license
rm -f "$CACHE_FILE"
start_server 2
assert_success_contains "TC05.1 activate short duration" "[activate] success" \
  "$BUILD_DIR/license_client" activate --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE"
sleep 3
assert_fail_contains "TC05.2 run after expiry" "license expired" \
  "$BUILD_DIR/license_client" run --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE" --grace 5

# TC06: tampered token
rm -f "$CACHE_FILE"
start_server 60
assert_success_contains "TC06.1 activate for tamper test" "[activate] success" \
  "$BUILD_DIR/license_client" activate --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE"
sed -i 's/^token=.*/token=INVALID_TOKEN/' "$CACHE_FILE"
assert_fail_contains "TC06.2 run with tampered token" "invalid token" \
  "$BUILD_DIR/license_client" run --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE" --grace 5

# TC07: machine mismatch
rm -f "$CACHE_FILE"
start_server 60
assert_success_contains "TC07.1 activate for machine mismatch" "[activate] success" \
  "$BUILD_DIR/license_client" activate --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE"
assert_fail_contains "TC07.2 run with machine mismatch" "cache machine mismatch" \
  "$BUILD_DIR/license_client" run --host 127.0.0.1 --port "$PORT" --machine "another-node" --cache "$CACHE_FILE" --grace 5

# TC08: wrong secret
assert_fail_contains "TC08 run with wrong secret" "invalid token" \
  "$BUILD_DIR/license_client" run --host 127.0.0.1 --port "$PORT" --machine "$MACHINE" --cache "$CACHE_FILE" --secret "WRONG_SECRET" --grace 5

log "[INFO] PASS=$PASS_COUNT FAIL=$FAIL_COUNT"

if [[ "$FAIL_COUNT" -eq 0 ]]; then
  log "[INFO] Regression result: PASS"
  exit 0
fi

log "[INFO] Regression result: FAIL"
exit 1
