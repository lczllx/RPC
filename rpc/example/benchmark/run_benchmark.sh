#!/bin/bash

# RPC 性能测试脚本

SERVER_PORT=8889
REGISTRY_PORT=8080
USE_DISCOVER=0

# 颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========== RPC 性能测试脚本 ==========${NC}"

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BIN_DIR="$BUILD_DIR/bin"

# 检查可执行文件是否存在
if [ ! -f "$BIN_DIR/benchmark_server" ]; then
    echo -e "${YELLOW}错误: 找不到 benchmark_server，请先编译项目${NC}"
    echo "运行: cd $PROJECT_ROOT && cmake -S . -B build && cmake --build build"
    exit 1
fi

if [ ! -f "$BIN_DIR/benchmark_client" ]; then
    echo -e "${YELLOW}错误: 找不到 benchmark_client，请先编译项目${NC}"
    echo "运行: cd $PROJECT_ROOT && cmake -S . -B build && cmake --build build"
    exit 1
fi

# 启动服务端（后台运行）
echo -e "${GREEN}启动服务端...${NC}"
"$BIN_DIR/benchmark_server" $SERVER_PORT $USE_DISCOVER $REGISTRY_PORT &
SERVER_PID=$!

# 等待服务端启动
sleep 2

# 检查服务端是否启动成功
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${YELLOW}错误: 服务端启动失败${NC}"
    exit 1
fi

echo -e "${GREEN}服务端已启动 (PID: $SERVER_PID)${NC}"

# 运行测试
# 注意：单线程和4线程使用相同请求数，便于对比
TEST_REQUESTS=20000

echo -e "\n${GREEN}========== 测试 1: 单线程延迟测试 ==========${NC}"
"$BIN_DIR/benchmark_client" single add $TEST_REQUESTS 0 0 $USE_DISCOVER 127.0.0.1 $SERVER_PORT $REGISTRY_PORT

echo -e "\n${GREEN}========== 测试 2: 多线程并发测试 (4线程) ==========${NC}"
"$BIN_DIR/benchmark_client" multi add $TEST_REQUESTS 4 0 $USE_DISCOVER 127.0.0.1 $SERVER_PORT $REGISTRY_PORT

echo -e "\n${GREEN}========== 测试 3: 框架开销测试 (echo方法) ==========${NC}"
"$BIN_DIR/benchmark_client" single echo 100000 0 0 $USE_DISCOVER 127.0.0.1 $SERVER_PORT $REGISTRY_PORT

echo -e "\n${GREEN}========== 测试 4: 吞吐量测试 (10秒) ==========${NC}"
"$BIN_DIR/benchmark_client" throughput add 0 0 10 $USE_DISCOVER 127.0.0.1 $SERVER_PORT $REGISTRY_PORT

# 停止服务端
echo -e "\n${GREEN}停止服务端...${NC}"
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null

echo -e "${GREEN}测试完成！${NC}"

