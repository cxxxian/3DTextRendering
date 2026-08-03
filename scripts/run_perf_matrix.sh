#!/usr/bin/env bash
# 三档构建并跑 #15 --bench。用法：
#   ./scripts/run_perf_matrix.sh
#   ./scripts/run_perf_matrix.sh --skip-build
#   ./scripts/run_perf_matrix.sh --frames 60 --warmup 15

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SKIP_BUILD=0
EXTRA=()
for a in "$@"; do
  if [[ "$a" == "--skip-build" ]]; then
    SKIP_BUILD=1
  else
    EXTRA+=("$a")
  fi
done

mkdir -p docs/perf
DATE="$(date +%Y-%m-%d)"

run_one() {
  local cfg="$1"
  local build_dir="build-perf-${cfg}"
  local out="docs/perf/${DATE}-${cfg}.md"
  if [[ "$SKIP_BUILD" -eq 0 ]]; then
    cmake -B "$build_dir" -DCMAKE_BUILD_TYPE="$cfg"
    cmake --build "$build_dir" -j
  fi
  local bin="$build_dir/Text3DDemo"
  if [[ ! -x "$bin" ]]; then
    echo "missing binary: $bin" >&2
    exit 1
  fi
  echo "=== bench $cfg → $out ==="
  "$bin" --bench --build-type "$cfg" --out "$out" "${EXTRA[@]+"${EXTRA[@]}"}"
}

run_one Debug
run_one RelWithDebInfo
run_one Release
echo "done. reports under docs/perf/"
