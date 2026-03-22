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
OPENSSL_CFLAGS=""
OPENSSL_LDFLAGS="-lssl -lcrypto"
if [[ -n "${CONDA_PREFIX:-}" ]] && [[ -f "${CONDA_PREFIX}/include/sqlite3.h" ]]; then
  SQLITE_CFLAGS="-I${CONDA_PREFIX}/include"
  SQLITE_LDFLAGS="-L${CONDA_PREFIX}/lib -lsqlite3"
  export LD_LIBRARY_PATH="${CONDA_PREFIX}/lib:${LD_LIBRARY_PATH:-}"
fi
if [[ -n "${CONDA_PREFIX:-}" ]] && [[ -f "${CONDA_PREFIX}/include/openssl/ssl.h" ]]; then
  OPENSSL_CFLAGS="-I${CONDA_PREFIX}/include"
  OPENSSL_LDFLAGS="-L${CONDA_PREFIX}/lib -lssl -lcrypto"
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
    $SQLITE_CFLAGS $OPENSSL_CFLAGS \
    "$ROOT_DIR/src/config.cpp" "$ROOT_DIR/src/common.cpp" "$ROOT_DIR/src/http.cpp" "$ROOT_DIR/src/log.cpp" "$ROOT_DIR/src/storage.cpp" "$ROOT_DIR/src/license_server_main.cpp" \
    $SQLITE_LDFLAGS $OPENSSL_LDFLAGS -o "$BUILD_DIR/license_server"

  g++ -std=c++17 -O2 -I"$ROOT_DIR/include" \
    $OPENSSL_CFLAGS \
    "$ROOT_DIR/src/config.cpp" "$ROOT_DIR/src/common.cpp" "$ROOT_DIR/src/http.cpp" "$ROOT_DIR/src/log.cpp" "$ROOT_DIR/src/license_client_main.cpp" \
    $OPENSSL_LDFLAGS -o "$BUILD_DIR/license_client"
fi

rm -f "$DB_FILE"
rm -f "$CACHE_DIR"/*.cache 2>/dev/null || true

"$BUILD_DIR/license_server" --port "$PORT" --duration "$DURATION" --db "$DB_FILE" > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

export BUILD_DIR HOST PORT TIMEOUT_MS GRACE_SEC CACHE_DIR MACHINE_PREFIX ACTIVATE_REQUESTS

percentile_latency_ms() {
  local file="$1"
  local pct="$2"
  local tmp_file="$ARTIFACT_DIR/.lat_${pct}_$$.tmp"

  awk -F'|' 'NF >= 2 && $2 ~ /^[0-9]+$/ { print $2 }' "$file" | sort -n > "$tmp_file"
  local n
  n=$(wc -l < "$tmp_file" | tr -d ' ')
  if [[ "${n:-0}" -eq 0 ]]; then
    rm -f "$tmp_file"
    echo "NA"
    return
  fi

  local idx=$(( (pct * n + 99) / 100 ))
  if [[ "$idx" -lt 1 ]]; then
    idx=1
  fi
  if [[ "$idx" -gt "$n" ]]; then
    idx="$n"
  fi
  sed -n "${idx}p" "$tmp_file"
  rm -f "$tmp_file"
}

failure_breakdown() {
  local file="$1"
  awk -F'|' '
    $1 == "FAIL" {
      key = ($3 == "" ? "unknown" : $3);
      cnt[key]++;
    }
    END {
      for (k in cnt) {
        printf "%s %d\n", k, cnt[k];
      }
    }
  ' "$file" | sort -k2,2nr -k1,1 | awk '
    BEGIN { first=1 }
    {
      if (!first) printf ",";
      printf "%s=%s", $1, $2;
      first=0;
    }
    END {
      if (first) {
        printf "none";
      }
      printf "\n";
    }
  '
}

activate_phase() {
  local start_ns end_ns elapsed_ms success fail qps p50 p95 p99 fail_detail
  start_ns=$(date +%s%N)
  seq 1 "$ACTIVATE_REQUESTS" | xargs -I{} -P "$PARALLEL" bash -lc '
    i="$1"
    machine="${MACHINE_PREFIX}-${i}"
    cache="${CACHE_DIR}/${machine}.cache"
    start_ns=$(date +%s%N)
    out=$("$BUILD_DIR/license_client" activate --host "$HOST" --port "$PORT" --machine "$machine" --cache "$cache" --timeout "$TIMEOUT_MS" --log-level ERROR 2>&1)
    rc=$?
    end_ns=$(date +%s%N)
    latency_ms=$(( (end_ns - start_ns) / 1000000 ))
    if [[ "$rc" -eq 0 ]]; then
      echo "OK|${latency_ms}|none"
    else
      cls="unknown"
      if [[ "$out" == *"request failed"* ]]; then
        cls="network_error"
      elif [[ "$out" == *"server reject"* ]]; then
        cls="server_reject"
      elif [[ "$out" == *"invalid server response"* ]]; then
        cls="response_parse_error"
      elif [[ "$out" == *"save cache failed"* ]]; then
        cls="cache_write_error"
      fi
      echo "FAIL|${latency_ms}|${cls}"
    fi
  ' _ {} >> "$ACTIVATE_RESULT"
  end_ns=$(date +%s%N)

  success=$(grep -c '^OK|' "$ACTIVATE_RESULT" || true)
  fail=$(grep -c '^FAIL|' "$ACTIVATE_RESULT" || true)
  elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
  qps=$(awk -v n="$ACTIVATE_REQUESTS" -v ms="$elapsed_ms" 'BEGIN { if (ms <= 0) { print "0.00" } else { printf "%.2f", (n*1000.0)/ms } }')
  p50=$(percentile_latency_ms "$ACTIVATE_RESULT" 50)
  p95=$(percentile_latency_ms "$ACTIVATE_RESULT" 95)
  p99=$(percentile_latency_ms "$ACTIVATE_RESULT" 99)
  fail_detail=$(failure_breakdown "$ACTIVATE_RESULT")

  echo "[bench] activate total=$ACTIVATE_REQUESTS success=$success fail=$fail elapsed_ms=$elapsed_ms qps=$qps"
  echo "[bench] activate latency_ms p50=$p50 p95=$p95 p99=$p99"
  echo "[bench] activate_failures $fail_detail"
  if [[ "$fail" -ne 0 ]]; then
    return 1
  fi
  return 0
}

run_phase() {
  local start_ns end_ns elapsed_ms success fail qps p50 p95 p99 fail_detail
  start_ns=$(date +%s%N)
  seq 1 "$RUN_REQUESTS" | xargs -I{} -P "$PARALLEL" bash -lc '
    i="$1"
    idx=$(( ((i - 1) % ACTIVATE_REQUESTS) + 1 ))
    machine="${MACHINE_PREFIX}-${idx}"
    cache="${CACHE_DIR}/${machine}.cache"
    start_ns=$(date +%s%N)
    out=$("$BUILD_DIR/license_client" run --host "$HOST" --port "$PORT" --machine "$machine" --cache "$cache" --grace "$GRACE_SEC" --timeout "$TIMEOUT_MS" --log-level ERROR 2>&1)
    rc=$?
    end_ns=$(date +%s%N)
    latency_ms=$(( (end_ns - start_ns) / 1000000 ))
    if [[ "$rc" -eq 0 ]]; then
      echo "OK|${latency_ms}|none"
    else
      cls="unknown"
      if [[ "$out" == *"no cache found"* ]]; then
        cls="no_cache"
      elif [[ "$out" == *"cache machine mismatch"* ]]; then
        cls="machine_mismatch"
      elif [[ "$out" == *"invalid token"* ]]; then
        cls="invalid_token"
      elif [[ "$out" == *"license expired"* ]]; then
        cls="license_expired"
      elif [[ "$out" == *"local clock rollback detected"* ]]; then
        cls="local_clock_rollback"
      elif [[ "$out" == *"heartbeat response parse failed"* ]]; then
        cls="heartbeat_parse_error"
      elif [[ "$out" == *"server time rollback detected"* ]]; then
        cls="server_time_rollback"
      elif [[ "$out" == *"grace exceeded"* ]]; then
        cls="grace_exceeded"
      elif [[ "$out" == *"request failed"* ]] || [[ "$out" == *"connect failed"* ]]; then
        cls="network_error"
      fi
      echo "FAIL|${latency_ms}|${cls}"
    fi
  ' _ {} >> "$RUN_RESULT"
  end_ns=$(date +%s%N)

  success=$(grep -c '^OK|' "$RUN_RESULT" || true)
  fail=$(grep -c '^FAIL|' "$RUN_RESULT" || true)
  elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
  qps=$(awk -v n="$RUN_REQUESTS" -v ms="$elapsed_ms" 'BEGIN { if (ms <= 0) { print "0.00" } else { printf "%.2f", (n*1000.0)/ms } }')
  p50=$(percentile_latency_ms "$RUN_RESULT" 50)
  p95=$(percentile_latency_ms "$RUN_RESULT" 95)
  p99=$(percentile_latency_ms "$RUN_RESULT" 99)
  fail_detail=$(failure_breakdown "$RUN_RESULT")

  echo "[bench] run total=$RUN_REQUESTS success=$success fail=$fail elapsed_ms=$elapsed_ms qps=$qps"
  echo "[bench] run latency_ms p50=$p50 p95=$p95 p99=$p99"
  echo "[bench] run_failures $fail_detail"
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
