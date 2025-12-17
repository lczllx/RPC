#!/bin/bash

# 快速构建脚本（简化版）
# 适用于已经配置好环境的用户

set -e

# 获取脚本所在目录（根目录的autobuild），然后切换到rpc目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RPC_DIR="$PROJECT_ROOT/rpc"
cd "$RPC_DIR"

echo "[INFO] 快速构建 RPC 框架..."

# 初始化子模块（在 rpc 目录下执行，git 会自动处理相对路径）
if [ ! -d "muduo" ] || [ -z "$(ls -A muduo 2>/dev/null)" ]; then
    echo "[INFO] 初始化 muduo 子模块..."
    git submodule update --init --recursive
fi

# 构建
mkdir -p build
cd build

if [ ! -f "CMakeCache.txt" ]; then
    cmake -DCMAKE_BUILD_TYPE=Release -DLCZ_RPC_BUILD_EXAMPLES=ON ..
fi

cmake --build . -j$(nproc)

echo "[INFO] 构建完成！"

