# RPC 框架

基于 muduo 网络库的 RPC 框架实现。

## 依赖要求

- CMake >= 3.16
- C++17 编译器（GCC >= 7 或 Clang >= 5）
- Boost 库
- pthread

## 构建步骤

### 1. 初始化 Git Submodule

首次克隆仓库后，需要初始化 muduo 子模块：

```bash
git submodule update --init --recursive
```

或者克隆时直接包含 submodule：

```bash
git clone --recursive <repository-url>
```

### 2. 构建项目

```bash
cd rpc
cmake -S . -B build
cmake --build build
```

### 3. 运行示例

```bash
# 运行消息测试
./build/example/message_test

# 运行分发器测试
./build/example/despacher_test
```

## 项目结构

```
rpc/
├── CMakeLists.txt      # 主 CMake 配置
├── src/                # RPC 框架核心代码
│   ├── client/         # 客户端实现
│   ├── server/         # 服务端实现
│   └── general/        # 通用组件
├── example/            # 示例程序
│   └── test/           # 测试用例
├── muduo/              # muduo 网络库（git submodule）
└── build/              # 构建输出目录（已忽略）
```

## 注意事项

- `muduo` 目录是 git submodule，需要单独初始化
- `build/` 目录是构建输出，已添加到 `.gitignore`
- 所有头文件使用 `src/` 前缀，例如：`#include "src/general/message.hpp"`

