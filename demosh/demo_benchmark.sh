#!/usr/bin/env bash
set -euo pipefail

# ====== 自动检测项目根目录 ======
find_root() {
    local dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    while [ "$dir" != "/" ]; do
        if [ -f "$dir/rpc/CMakeLists.txt" ] || [ -f "$dir/.git" ]; then
            echo "$dir"; return 0
        fi
        dir="$(dirname "$dir")"
    done
    return 1
}
ROOT_DIR="$(find_root)"
if [ -z "$ROOT_DIR" ]; then
    echo "[ERROR] 找不到项目根目录" >&2
    exit 1
fi

BENCH_DIR="${ROOT_DIR}/rpc/example/benchmark"
BIN_DIR="${ROOT_DIR}/rpc/build/bin"

if [[ ! -d "${BIN_DIR}" ]]; then
  echo "[ERROR] 未找到 ${BIN_DIR}，请先编译"
  exit 1
fi

echo "== Demo: Benchmark（JSON vs Protobuf）=="
echo "BIN_DIR: ${BIN_DIR}"
echo "BENCH_DIR: ${BENCH_DIR}"
echo

if [[ ! -x "${BIN_DIR}/benchmark_server" || ! -x "${BIN_DIR}/benchmark_client" ]]; then
  echo "[ERROR] 缺少 Protobuf benchmark 可执行文件：benchmark_server / benchmark_client"
  exit 1
fi
if [[ ! -x "${BIN_DIR}/benchmark_server_json" || ! -x "${BIN_DIR}/benchmark_client_json" ]]; then
  echo "[ERROR] 缺少 JSON benchmark 可执行文件：benchmark_server_json / benchmark_client_json"
  exit 1
fi

echo ">>> 1) 运行 JSON benchmark"
(cd "${BENCH_DIR}" && bash run_benchmark_json.sh)
echo

echo ">>> 2) 运行 Protobuf benchmark"
(cd "${BENCH_DIR}" && bash run_benchmark.sh)
echo

echo "完成"
