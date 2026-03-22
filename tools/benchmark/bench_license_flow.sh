#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
ARTIFACT_DIR="$ROOT_DIR/test_artifacts/benchmark"
SERVER_LOG="$ARTIFACT_DIR/bench_server.log"
ACTIVATE_RESULT="$ARTIFACT_DIR/activate_results.txt"
RUN_RESULT="$ARTIFACT_DIR/run_results.txt"
DB_FILE="$ARTIFACT_DIR/bench_store.db"
CACHE_DIR="$ARTIFACT_DIR/cache"

HOST="127.0.0.1"
PORT=18093
PARALLEL=8
ACTIVATE_REQUESTS=40
RUN_REQUESTS=40
DURATION=180
GRACE_SEC=5
TIMEOUT_MS=1200
MACHINE_PREFIX="bench-node"
SKIP_BUILD=0

SQLITE_CFLAGS=""
SQLITE_LDFLAGS="-lsqlite3"
if [[ -n "${CONDA_PREFIX:-}" ]] && [[ -f "${CONDA_PREFIX}/include/sqlite3.h" ]]; then
  SQLITE_CFLAGS="-I${CONDA_PREFIX}/include"
  SQLITE_LDFLAGS="-L${CONDA_PREFIX}/lib -lsqlite3"
  export LD_LIBRARY_PATH="${CONDA_PREFIX}/lib:${LD_LIBRARY_PATH:-}"
fi

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  --host <ip>                 default: 127.0.0.1
  --port <port>               default: 18093
  --parallel <n>              default: 8
  --activate-requests <n>     default: 40
  --run-requests <n>          default: 40
  --duration <sec>            default: 180
  --grace <sec>               default: 5
  --timeout <ms>              default: 1200
  --machine-prefix <prefix>   default: bench-node
  --skip-build                reuse existing binaries in build/
  --help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host) HOST="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --parallel) PARALLEL="$2"; shift 2 ;;
    --activate-requests) ACTIVATE_REQUESTS="$2"; shift 2 ;;
    --run-requests) RUN_REQUESTS="$2"; shift 2 ;;
    --duration) DURATION="$2"; shift 2 ;;
    --grace) GRACE_SEC="$2"; shift 2 ;;
    --timeout) TIMEOUT_MS="$2"; shift 2 ;;
    --machine-prefix) MACHINE_PREFIX="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    --help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

mkdir -p "$BUILD_DIR" "$ARTIFACT_DIR" "$CACHE_DIR"
: > "$ACTIVATE_RESULT"
: > "$RUN_RESULT"
: > "$SERVER_LOG"

SERVER_PID=""

cleanup() {
  if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  g++ -std=c++17 -O2 -I"$ROOT_DIR/include" \
    $SQLITE_CFLAGS \
    "$ROOT_DIR/src/config.cpp" "$ROOT_DIR/src/common.cpp" "$ROOT_DIR/src/http.cpp" "$ROOT_DIR/src/log.cpp" "$ROOT_DIR/src/storage.cpp" "$ROOT_DIR/src/license_server_main.cpp" \
    $SQLITE_LDFLAGS -o "$BUILD_DIR/license_server"

  g++ -std=c++17 -O2 -I"$ROOT_DIR/include" \
    "$ROOT_DIR/src/config.cpp" "$ROOT_DIR/src/common.cpp" "$ROOT_DIR/src/http.cpp" "$ROOT_DIR/src/log.cpp" "$ROOT_DIR/src/license_client_main.cpp" \
    -o "$BUILD_DIR/license_client"
fi

rm -f "$DB_FILE"
rm -f "$CACHE_DIR"/*.cache 2>/dev/null || true

"$BUILD_DIR/license_server" --port "$PORT" --duration "$DURATION" --db "$DB_FILE" > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

export BUILD_DIR HOST PORT TIMEOUT_MS GRACE_SEC CACHE_DIR MACHINE_PREFIX ACTIVATE_REQUESTS

activate_phase() {
  local start_ns end_ns elapsed_ms success fail qps
  start_ns=$(date +%s%N)
  seq 1 "$ACTIVATE_REQUESTS" | xargs -I{} -P "$PARALLEL" bash -lc '
    i="$1"
    machine="${MACHINE_PREFIX}-${i}"
    cache="${CACHE_DIR}/${machine}.cache"
    if "$BUILD_DIR/license_client" activate --host "$HOST" --port "$PORT" --machine "$machine" --cache "$cache" --timeout "$TIMEOUT_MS" --log-level ERROR >/dev/null 2>&1; then
      echo OK
    else
      echo FAIL
    fi
  ' _ {} >> "$ACTIVATE_RESULT"
  end_ns=$(date +%s%N)

  success=$(grep -c '^OK$' "$ACTIVATE_RESULT" || true)
  fail=$(grep -c '^FAIL$' "$ACTIVATE_RESULT" || true)
  elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
  qps=$(awk -v n="$ACTIVATE_REQUESTS" -v ms="$elapsed_ms" 'BEGIN { if (ms <= 0) { print "0.00" } else { printf "%.2f", (n*1000.0)/ms } }')

  echo "[bench] activate total=$ACTIVATE_REQUESTS success=$success fail=$fail elapsed_ms=$elapsed_ms qps=$qps"
  if [[ "$fail" -ne 0 ]]; then
    return 1
  fi
  return 0
}

run_phase() {
  local start_ns end_ns elapsed_ms success fail qps
  start_ns=$(date +%s%N)
  seq 1 "$RUN_REQUESTS" | xargs -I{} -P "$PARALLEL" bash -lc '
    i="$1"
    idx=$(( ((i - 1) % ACTIVATE_REQUESTS) + 1 ))
    machine="${MACHINE_PREFIX}-${idx}"
    cache="${CACHE_DIR}/${machine}.cache"
    if "$BUILD_DIR/license_client" run --host "$HOST" --port "$PORT" --machine "$machine" --cache "$cache" --grace "$GRACE_SEC" --timeout "$TIMEOUT_MS" --log-level ERROR >/dev/null 2>&1; then
      echo OK
    else
      echo FAIL
    fi
  ' _ {} >> "$RUN_RESULT"
  end_ns=$(date +%s%N)

  success=$(grep -c '^OK$' "$RUN_RESULT" || true)
  fail=$(grep -c '^FAIL$' "$RUN_RESULT" || true)
  elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
  qps=$(awk -v n="$RUN_REQUESTS" -v ms="$elapsed_ms" 'BEGIN { if (ms <= 0) { print "0.00" } else { printf "%.2f", (n*1000.0)/ms } }')

  echo "[bench] run total=$RUN_REQUESTS success=$success fail=$fail elapsed_ms=$elapsed_ms qps=$qps"
  if [[ "$fail" -ne 0 ]]; then
    return 1
  fi
  return 0
}

echo "[bench] start host=$HOST port=$PORT parallel=$PARALLEL activate_requests=$ACTIVATE_REQUESTS run_requests=$RUN_REQUESTS"

if ! activate_phase; then
  echo "[bench] result: FAIL (activate phase)"
  exit 1
fi

if ! run_phase; then
  echo "[bench] result: FAIL (run phase)"
  exit 1
fi

echo "[bench] result: PASS"
