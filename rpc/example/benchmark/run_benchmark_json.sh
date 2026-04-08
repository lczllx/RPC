#!/bin/sh
# RPC 性能测试脚本（JSON 序列化，原版副本）

SERVER_PORT=8889
REGISTRY_PORT=8080
USE_DISCOVER=0

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

printf '%b\n' "${GREEN}========== RPC 性能测试脚本（JSON 原版副本）==========${NC}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BIN_DIR="$BUILD_DIR/bin"

if [ ! -f "$BIN_DIR/benchmark_server_json" ]; then
    printf '%b\n' "${YELLOW}错误: 找不到 benchmark_server_json，请先编译${NC}"
    exit 1
fi
if [ ! -f "$BIN_DIR/benchmark_client_json" ]; then
    printf '%b\n' "${YELLOW}错误: 找不到 benchmark_client_json，请先编译${NC}"
    exit 1
fi

printf '%b\n' "${GREEN}启动服务端（JSON）...${NC}"
"$BIN_DIR/benchmark_server_json" $SERVER_PORT $USE_DISCOVER $REGISTRY_PORT &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    printf '%b\n' "${YELLOW}错误: 服务端启动失败${NC}"
    exit 1
fi

TEST_REQUESTS=20000

printf '\n%b\n' "${GREEN}========== 测试 1: 单线程 ==========${NC}"
"$BIN_DIR/benchmark_client_json" single add $TEST_REQUESTS 0 0 $USE_DISCOVER 127.0.0.1 $SERVER_PORT $REGISTRY_PORT

printf '\n%b\n' "${GREEN}========== 测试 2: 多线程 (4) ==========${NC}"
"$BIN_DIR/benchmark_client_json" multi add $TEST_REQUESTS 4 0 $USE_DISCOVER 127.0.0.1 $SERVER_PORT $REGISTRY_PORT

printf '\n%b\n' "${GREEN}========== 测试 3: echo 10万 ==========${NC}"
"$BIN_DIR/benchmark_client_json" single echo 100000 0 0 $USE_DISCOVER 127.0.0.1 $SERVER_PORT $REGISTRY_PORT

printf '\n%b\n' "${GREEN}========== 测试 4: 吞吐量 10秒 ==========${NC}"
"$BIN_DIR/benchmark_client_json" throughput add 0 0 10 $USE_DISCOVER 127.0.0.1 $SERVER_PORT $REGISTRY_PORT

# 测试 5: 大 payload (100KB echo)，与 run_benchmark.sh 同一场景对比
printf '\n%b\n' "${GREEN}========== 测试 5: 大 payload (echo 100KB x 1000 次) ==========${NC}"
"$BIN_DIR/benchmark_client_json" single echo 1000 0 0 $USE_DISCOVER 127.0.0.1 $SERVER_PORT $REGISTRY_PORT 100000

printf '\n%b\n' "${GREEN}停止服务端...${NC}"
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
printf '%b\n' "${GREEN}测试完成！${NC}"
