#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH_DIR="${ROOT_DIR}/rpc/example/benchmark"
BIN_DIR="${ROOT_DIR}/rpc/build/bin"

if [[ ! -d "${BIN_DIR}" ]]; then
  echo "[ERROR] 未找到 ${BIN_DIR}，请先编译：cd ${ROOT_DIR}/rpc && mkdir -p build && cd build && cmake .. && make -j"
  exit 1
fi

echo "== Demo 1: Benchmark（JSON vs Protobuf）=="
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

echo "完成。建议面试时强调：100KB echo 场景 P99 从 1.96ms 降到 0.72ms（见 rpc/docs/protobuf_implementation.md）。"

