# =============================================================================
# LCZ RPC — 多阶段镜像
# -----------------------------------------------------------------------------
# 构建（在仓库根 RPC/ 执行，且 rpc/muduo 子模块已初始化）：
#   git submodule update --init --recursive
#   docker build -t lcz-rpc:local .
#
# 运行示例（需自行编排网络；端口按 example 默认）：
#   docker run --rm -it lcz-rpc:local
#   docker run --rm -p 7070:7070 lcz-rpc:local /opt/rpc/bin/test1_registry_server
# =============================================================================

# ---------- 阶段 1：编译 ----------
FROM ubuntu:22.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    g++ \
    make \
    libboost-dev \
    libjsoncpp-dev \
    libcurl4-openssl-dev \
    protobuf-compiler \
    libprotobuf-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY rpc/ /src/rpc/

WORKDIR /src/rpc/build
RUN cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DLCZ_RPC_BUILD_EXAMPLES=ON \
    -DLCZ_RPC_BUILD_TESTS=OFF
RUN cmake --build . -j$(nproc)

# ---------- 阶段 2：运行（仅运行时库 + 可执行文件）----------
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive

# 与可执行文件动态链接的常见依赖（无 gtest，镜像更小）
RUN apt-get update && apt-get install -y --no-install-recommends \
    libjsoncpp25 \
    libprotobuf23 \
    libcurl4 \
    zlib1g \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/rpc/build/bin /opt/rpc/bin

WORKDIR /opt/rpc
ENV PATH="/opt/rpc/bin:${PATH}"

# 默认进入 shell，便于你手动起 registry / server / client
CMD ["/bin/bash", "-lc", "echo 'LCZ RPC — 可执行文件位于 /opt/rpc/bin'; ls -la /opt/rpc/bin"]
