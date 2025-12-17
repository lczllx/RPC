#!/bin/bash

# RPC框架自动构建脚本
# 功能：检查依赖、拉取muduo子模块、配置并编译项目

set -e  # 遇到错误立即退出

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查命令是否存在
check_command() {
    if ! command -v $1 &> /dev/null; then
        print_error "$1 未安装，请先安装 $1"
        exit 1
    fi
}

# 获取脚本所在目录（根目录的autobuild），然后切换到rpc目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RPC_DIR="$PROJECT_ROOT/rpc"
cd "$RPC_DIR"

print_info "开始构建 RPC 框架..."
print_info "脚本目录: $SCRIPT_DIR"
print_info "项目根目录: $PROJECT_ROOT"
print_info "RPC 目录: $RPC_DIR"

# 1. 检查必要的工具
print_info "检查构建工具..."
check_command git
check_command cmake
check_command g++

# 检查CMake版本
CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
CMAKE_MAJOR=$(echo $CMAKE_VERSION | cut -d'.' -f1)
CMAKE_MINOR=$(echo $CMAKE_VERSION | cut -d'.' -f2)
if [ "$CMAKE_MAJOR" -lt 3 ] || ([ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 16 ]); then
    print_error "CMake 版本过低，需要 >= 3.16，当前版本: $CMAKE_VERSION"
    exit 1
fi
print_info "CMake 版本: $CMAKE_VERSION ✓"

# 检查g++版本
GXX_VERSION=$(g++ --version | head -n1 | cut -d' ' -f4)
print_info "g++ 版本: $GXX_VERSION ✓"

# 2. 检查并安装系统依赖（可选）
print_info "检查系统依赖..."

# 检查Boost（muduo需要）
if ! pkg-config --exists boost 2>/dev/null && ! ldconfig -p | grep -q libboost_system; then
    print_warn "未检测到 Boost 库，muduo 需要 Boost"
    print_warn "Ubuntu/Debian: sudo apt-get install libboost-dev"
    print_warn "CentOS/RHEL: sudo yum install boost-devel"
    read -p "是否继续构建？(y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
else
    print_info "Boost 库已安装 ✓"
fi

# 检查jsoncpp（可选，CMake会自动下载）
if pkg-config --exists jsoncpp 2>/dev/null || ldconfig -p | grep -q libjsoncpp; then
    print_info "检测到系统安装的 jsoncpp，将优先使用"
else
    print_info "未检测到系统 jsoncpp，CMake 将自动下载"
fi

# 3. 初始化并更新git子模块（muduo）
print_info "初始化 git 子模块（muduo）..."

# 在 rpc 目录下执行 git submodule（git 会自动处理相对路径）
if [ ! -d "muduo" ] || [ -z "$(ls -A muduo 2>/dev/null)" ]; then
    print_info "muduo 目录为空，初始化子模块..."
    git submodule update --init --recursive
else
    print_info "muduo 目录已存在，更新子模块..."
    git submodule update --recursive --remote 2>/dev/null || git submodule update --recursive
fi

if [ ! -d "muduo" ] || [ -z "$(ls -A muduo 2>/dev/null)" ]; then
    print_error "muduo 子模块初始化失败"
    exit 1
fi

print_info "muduo 子模块已就绪 ✓"

# 4. 创建构建目录
BUILD_DIR="build"
print_info "创建构建目录: $BUILD_DIR"
mkdir -p "$BUILD_DIR"

# 5. 配置CMake
print_info "配置 CMake..."
cd "$BUILD_DIR"

# 检查是否已有CMakeCache，如果有则询问是否清理
if [ -f "CMakeCache.txt" ]; then
    print_warn "检测到已存在的构建配置"
    read -p "是否清理并重新配置？(y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        print_info "清理构建目录..."
        rm -rf *
    fi
fi

# CMake配置
CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DLCZ_RPC_BUILD_EXAMPLES=ON
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

print_info "CMake 配置参数: ${CMAKE_ARGS[*]}"
cmake "${CMAKE_ARGS[@]}" ..

if [ $? -ne 0 ]; then
    print_error "CMake 配置失败"
    exit 1
fi

print_info "CMake 配置成功 ✓"

# 6. 编译项目
print_info "开始编译项目..."
CPU_CORES=$(nproc 2>/dev/null || echo 4)
print_info "使用 $CPU_CORES 个CPU核心进行并行编译"

cmake --build . -j$CPU_CORES

if [ $? -ne 0 ]; then
    print_error "编译失败"
    exit 1
fi

print_info "编译成功 ✓"

# 7. 显示构建结果
cd "$RPC_DIR"
echo ""
print_info "=========================================="
print_info "构建完成！"
print_info "=========================================="
echo ""
print_info "可执行文件位置:"
if [ -d "$BUILD_DIR/bin" ]; then
    find "$BUILD_DIR/bin" -type f -executable | while read file; do
        echo "  - $file"
    done
fi

if [ -d "$BUILD_DIR/example" ]; then
    find "$BUILD_DIR/example" -type f -executable | while read file; do
        echo "  - $file"
    done
fi

echo ""
print_info "运行示例:"
echo "  # 注册中心"
echo "  ./$BUILD_DIR/bin/registry_server  # 如果存在"
echo ""
echo "  # RPC 服务端"
echo "  ./$BUILD_DIR/example/test/test1/rpc_server"
echo ""
echo "  # RPC 客户端"
echo "  ./$BUILD_DIR/example/test/test1/rpc_client"
echo ""
print_info "构建脚本执行完成！"

