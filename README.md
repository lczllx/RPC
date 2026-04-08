# LCZ RPC

作者：lczllx  
邮箱：2181719471@qq.com  
GitHub：https://github.com/lczllx/RPC  
开发环境：Ubuntu，VS Code  
编译器：g++  
语言：C++11  
网络：muduo  
序列化：jsoncpp（JSON）/ Protobuf  
构建：CMake  

---

这是我用 C++11 + muduo 写的一个轻量 RPC，JSON、Protobuf 都能走，带注册中心、心跳、负载均衡，调用有同步、Future、回调几种。

网络层用 muduo，事件循环、非阻塞 IO、定时器都在这套里，多 IO 线程时用 `EventLoopThreadPool`。RPC 的帧格式、序列化、注册中心不在 muduo 里，在本仓库里实现。

JSON 用 jsoncpp，好调试。Protobuf 走 `call_proto` / `registerProtoHandler`，和压测里的二进制路径一致。CMake 用来生成 proto、拉 muduo 子模块。

TCP 要自己定帧，不然粘包不好拆。LV：`长度 + 类型 + id + body`，收齐一帧再反序列化，`msg_type` 给分发，`id` 对上请求和响应。

---

## 1. 项目简介

RPC 从发请求到收响应整条链路是齐的，benchmark 里对比了 JSON 和 Protobuf。注册中心、心跳、怎么选节点也写了，能跑通、能演示。

---

## 2. 压测

优化过程中 QPS 从约 20.2k（2C2G）到约 31k（4C8G）再到约 5 万（4C8G）。下面是一次跑出来的数，换机器会有偏差。

大 payload（`echo 100KB × 1000`）：P99 **1.96ms → 0.72ms**，QPS **702 → 1607**（约 2.29 倍），平均延迟也有下降。

小 payload（add、短 echo）：Protobuf 相对 JSON 的 QPS 大约高 **26%～75%**，延迟略低，以脚本输出为准。

测了：单线程 add、多线程 add、跑 10 秒吞吐、100KB echo。

环境：`4C8G` 云机、`Ubuntu 22.04`、`g++（版本待补）`、`-O3`、本机回环。

| 场景 | JSON | Protobuf | 提升 |
|---|---:|---:|---:|
| 小 payload QPS（多线程 add） | 31,546 | 50,125 | +59% |
| 大 payload QPS（100KB echo） | 702.74 | 1,607.72 | 2.29x |
| 大 payload P99 | 1.96ms | 0.72ms | 2.72x |

---

## 3. 快速开始

**一键构建（推荐）**
```bash
cd RPC
bash autobuild/quick_build.sh
```

完整构建（依赖检查、子模块等）：

```bash
cd RPC
bash autobuild/build.sh
```

**依赖**
```bash
# Ubuntu 
sudo apt update
sudo apt install -y cmake g++ libjsoncpp-dev protobuf-compiler libprotobuf-dev
```

**编译**
```bash
cd rpc
mkdir -p build && cd build
cmake ..
make -j
```

**示例**
项目内置示例位于 `rpc/example`，常用包括：
- 注册中心：`test/test1/registry_server.cc`
- Provider：`test/test1/rpc_server.cc`
- Consumer：`test/test1/rpc_client.cc`
- Benchmark：`benchmark/benchmark_server.cc`、`benchmark/benchmark_client.cc`

> 可执行文件默认输出到 `build/bin`（含 benchmark 系列）。

**压测脚本**

```bash
cd rpc/example/benchmark
./run_benchmark.sh
sh run_benchmark_json.sh
```

---

## 4. 流程图

### 调用链

![RPC 调用流程图](https://raw.githubusercontent.com/lczllx/RPC/main/flowchat/flow-rpc-call.png)

`RpcClient` / `RpcCaller` 发出请求，服务端按 method 进入 `registerProtoHandler`，响应原路返回。

### 注册与发现

![服务注册发现流程图](https://raw.githubusercontent.com/lczllx/RPC/main/flowchat/flow-registry.png)

服务方注册并定时心跳。调用方按 method 向注册中心取节点列表，再在本地选择实例。

### 心跳与实例摘除

![心跳保活和失效剔除](https://raw.githubusercontent.com/lczllx/RPC/main/flowchat/flow-heartbeat.png)

心跳间隔 **10s**，**15s** 内无更新则从列表移除该实例。

### 客户端超时

![客户端超时控制](https://raw.githubusercontent.com/lczllx/RPC/main/flowchat/flow-timeout.png)

按 `rid` 注册 muduo 定时器，超时先返回 `TIMEOUT`，响应先到达则取消定时器。迟到响应丢弃，避免与超时重复处理。

---

## 5. 架构

角色：

- **Provider**：对外提供服务，注册、处理 RPC。
- **Consumer**：发 RPC。
- **Registry**：记有哪些实例、心跳、上下线。

调用链：

```text
Consumer
  -> RpcClient / RpcCaller / Requestor
  -> 序列化（JSON 或 Protobuf）
  -> LV 帧封包
  -> muduo 发 TCP
  -> Provider 拆包、反序列化、进业务
  -> 响应往回走
```

注册与发现：

```text
Provider --注册/心跳--> Registry <--查列表-- Consumer
   |                         |
   +----- 超时未心跳则摘掉 --------+
```

---

## 6. 功能

**序列化**  
JSON（jsoncpp）好调试。Protobuf 走 `REQ_RPC_PROTO` 等，包一大和 JSON 差距就明显。两条路都留着，想用哪个用哪个。

**LV 协议**  
`LVProtocol` 帧格式：

```text
| 4B total_len | 4B msg_type | 4B id_len | id | body |
```

用 `total_len` 判断一帧是否收齐，再反序列化。`msg_type` 交给 `MessageFactory`，`id` 对应请求与响应。整型字段按网络字节序写。`total_len` 有上限，防止异常大包，具体数值见代码。

**注册、心跳、选节点**  
心跳 **10s**，Registry **5s** 扫一圈，**15s** 没心跳就当掉线。负载均衡：`ROUND_ROBIN`、`RANDOM`、`SOURCE_HASH`、`LOWEST_LOAD`。调用方式：同步、Future、回调。超时直接失败，不会自动帮你重试。

**Topic**  
发布订阅，转发策略有 `BROADCAST`、`FANOUT` 等枚举，和 RPC 共用底层消息和网络。

---

## 7. 实现要点

**粘包**  
靠 `total_len` 判断一帧收没收齐，`canProcessed()` 不过就接着攒，不瞎反序列化，避免越界和脏数据。

**请求和响应对上号**  
每个请求一个 `rid`，客户端 `rid -> ReqDescribe`，回来按 `rid` 分到同步 / future / 回调。在途请求用 `unordered_map` + 一把锁，写得简单，并发高了会抢锁，以后要优化再说。

**超时**  
`runAfter` 挂定时器，响应先到就取消。超时先到就丢后面的包，同一个 `rid` 不会又超时又当成功。

**实例掉线**  
Registry 定时扫过期实例并通知。Consumer 收到就清连接池，少往已经下线的节点打。

---

## 8. 目录

```text
RPC/
├── README.md
└── rpc/
    ├── src/
    │   ├── client/
    │   ├── server/
    │   └── general/
    ├── proto/
    │   └── rpc_envelope.proto
    ├── docs/
    ├── example/
    │   ├── benchmark/
    │   └── test/
    └── CMakeLists.txt
```

---

## 9. 规划实现

- [ ] 监控：QPS、延迟分位、错误码
- [ ] 熔断、限流、重试
- [ ] 注册中心多活（50%）
- [ ] 部署、容器化
- [ ] 单测和回归
