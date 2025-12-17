# 轻量级、可扩展的分布式RPC服务框架
> 基于 muduo 的高性能分布式 RPC/Topic 框架，提供服务注册发现、JSON 序列化、主题发布订阅与多种负载均衡策略。

- **作者**：lczllx

- **联系方式**：2181719471@qq.com

开发环境：Ubuntu VS Code

编译器：g++

编程语言：C++
---
### 依赖
- Linux / macOS，g++ ≥ 9 或 clang ≥ 10
- CMake ≥ 3.16
- jsoncpp（FetchContent 自动获取）
- muduo（本仓库 submodule，默认关闭其示例）

### 构建
从项目根目录运行构建脚本：
```bash
./autobuild/build.sh
```

### 运行示例
构建完成后，在 `rpc` 目录下运行示例：
```bash
cd rpc

# 注册中心 + 消息分发
./build/example/despacher_server_test
./build/example/despacher_client_test

# RPC 服务 & 客户端（服务发现 + 同步/异步）
./build/example/test/test1/rpc_server
./build/example/test/test1/rpc_client

# Topic、Benchmark
./build/example/despacher_test
./build/example/benchmark/benchmark_client
```

---

## 功能速览
- **JSON 消息协议**：自定义了 Header + Payload，统一校验字段的合法性和错误码。
- **注册发现**：RegistryServer 管理服务-方法-实例映射，支持心跳、负载上报、过期剔除及离线通知。
- **RPC 服务治理**：RpcServer + RpcRouter 提供参数的校验、回调执行以及统一响应。
- **客户端能力**：同步/异步调用、Future 回调（重载实现）、策略化负载均衡、服务缓存。
- **Topic 系统**：TopicServer 支持广播、轮询、扇出、源哈希、优先级、冗余等策略。
- **网络抽象**：抽象出BaseServer/BaseClient结合LVProtocol 封装 muduo库，便于扩展其他事件库。

---

## 架构概览
1. **消息层**：`BaseMessage` 抽象派生出 Rpc/Service/Topic 请求与响应，统一 JSON 序列化。
2. **分发层**：`Dispacher` 按 `MsgType` 将消息路由到 `RpcRouter`、`TopicManager`、`ProviderManager`。
3. **业务层**：RpcServer/RpcClient/RegistryServer/TopicServer 组合实现服务注册、调用与发布订阅。
4. **治理层**：心跳、负载上报、服务缓存、下线通知由 Registry 模块维护。

![整体流程](flowchat/26a5b5b6e1576586f7ba2d3a4ebfdd4c.png)

---

## 核心模块
### 消息与协议
- **LVProtocol**：长度 + 类型 + 消息 ID + Body，解决粘包拆包问题。
- **消息体系**：所有消息继承 `BaseMessage`，实现 `serialize/unserialize/check`，便于扩展。
- **类型**：RPC 请求/响应、服务注册/发现、Topic 请求/响应等，均基于 JSON 载荷。

### RegistryServer
- `ProviderManager`：注册、下线、心跳、负载上报。
- `DiscoverManager`：服务发现、客户端缓存、失效通知。
- `PwithDManager`：统一调度，定时扫描过期 Provider 并广播下线。
- `ClientRegistry`：嵌入服务端，自动完成方法注册和健康上报。

### RpcServer & RpcRouter
- RpcServer 负责网络监听、接入注册中心、定时上报负载。
- RpcRouter 通过 `ServiceManager` 查找 ServiceDescribe，校验参数并调用回调函数。
- ServiceFactory 支持声明方法名、参数类型、返回类型、绑定 C++ 函数。

### RpcClient & Requestor
- `RpcClient` 可开启 `ClientDiscover`，与注册中心保持长连并感知下线事件。
- `Requestor` 维护请求 ID 和对应 回调/Future 映射，提供同步 call、异步 Future、回调等接口。
- `setloadbalanceStrategy` 支持轮询、最小负载等策略。

### TopicServer
- `TopicManager` 管理 Topic 生命周期，负责订阅、取消订阅、删除、发布。
- Topic 实体实现多种投递策略（广播/轮询/扇出/源哈希/优先级/冗余）。
- 与 RpcClient 共用基础网络与消息协议。

### 网络层
- `BaseServer`/`BaseClient` 抽象底层实现，`MuduoServer`/`MuduoClient` 实现具体落地。
- `ServerFactory`/`ClientFactory` 提供统一创建接口，支持扩展其他事件库。

---

## 核心流程
### RPC 调用
![RPC调用流程](flowchat/83485bb481a95b0a48f6b127b3d7ff15.jpg)

1. RpcCaller 构造 `RpcRequest`，生成 UUID，Requestor 记录回调映射。
2. Dispacher 根据 `MsgType` 将请求派发给 RpcRouter。
3. RpcRouter 查询 ServiceDescribe、校验参数、执行回调并封装 `RpcResponse`。
4. Requestor 匹配响应 ID，触发 Future/回调，将结果返回给调用方。

### 服务注册与发现
![注册发现流程](flowchat/2a1092c377167c55cd48a8d8f234d893.jpg)

1. Provider 启动后通过 `ClientRegistry::methodRegistry` 注册方法。
2. RegistryServer 保存映射并接收 HEARTBEAT/LOAD_REPORT。
3. Consumer 侧 RpcClient 命中本地缓存即用，否则向注册中心 DISCOVER。
4. Discoverer 根据策略返回 HostDetail，并监听服务下线通知。

### 发布订阅
![Topic流程](flowchat/c0d65144bad482537c5a761a9e3bea56.jpg)

1. Subscriber 发送 SUBSCRIBE，TopicManager 创建/更新 Topic。
2. Publisher 发布消息，Topic 根据策略向订阅者推送。
3. 支持广播、轮询、扇出、源哈希、优先级、冗余等策略，满足不同 QoS。

---

## 示例代码
### 服务端：注册方法 + 自动服务发现
```1:35:example/test/test1/rpc_server.cc
#include "src/server/rpc_server.hpp"
...
req_factory->setMethodName("add");
req_factory->setServiceCallback(add);
lcz_rpc::server::RpcServer server(
    lcz_rpc::HostInfo("127.0.0.1", 8889),
    true,
    lcz_rpc::HostInfo("127.0.0.1", 8080));
server.registerMethod(req_factory->build());
server.start();
```

### Requestor：请求 ID → 回调/Future 映射
```20:128:src/client/requestor.hpp
class Requestor {
public:
    using ReqCallback = std::function<void(const BaseMessage::ptr&)>;
    using AsyncResponse = std::future<BaseMessage::ptr>;
    struct ReqDescribe {
        using ptr = std::shared_ptr<ReqDescribe>;
        ReqType reqtype;
        ReqCallback callback;
        std::promise<BaseMessage::ptr> response;
        BaseMessage::ptr request;
    };

    void onResponse(const BaseConnection::ptr& conn, BaseMessage::ptr& msg) {
        std::string id = msg->rid();
        ReqDescribe::ptr req_desc = getDesc(id);
        if (req_desc.get() == nullptr) {
            ELOG("收到 %s 响应，但消息描述不存在", id.c_str());
            return;
        }
        if (req_desc->reqtype == ReqType::ASYNC) {
            req_desc->response.set_value(msg);
        } else if (req_desc->reqtype == ReqType::CALLBACK) {
            if (req_desc->callback) req_desc->callback(msg);
        } else {
            ELOG("未知请求类型");
        }
        delDesc(id);
    }

    bool send(const BaseConnection::ptr& conn,
              const BaseMessage::ptr& req,
              AsyncResponse& async_resp) {
        ReqDescribe::ptr req_desc = newDesc(req, ReqType::ASYNC);
        if (req_desc.get() == nullptr) return false;
        conn->send(req);
        async_resp = req_desc->response.get_future();
        return true;
    }

    bool send(const BaseConnection::ptr& conn,
              const BaseMessage::ptr& req,
              BaseMessage::ptr& resp) {
        AsyncResponse async_resp;
        if (send(conn, req, async_resp) == false) return false;
        resp = async_resp.get();
        return true;
    }

    bool send(const BaseConnection::ptr& conn,
              const BaseMessage::ptr& req,
              const ReqCallback& cb) {
        ReqDescribe::ptr req_desc = newDesc(req, ReqType::CALLBACK, cb);
        if (req_desc.get() == nullptr) return false;
        conn->send(req);
        return true;
    }

private:
    ReqDescribe::ptr newDesc(const BaseMessage::ptr& req,
                             ReqType req_type,
                             const ReqCallback& cb = ReqCallback()) {
        std::unique_lock<std::mutex> lock(_mutex);
        ReqDescribe::ptr req_desc = std::make_shared<ReqDescribe>();
        req_desc->reqtype = req_type;
        req_desc->request = req;
        if (req_type == ReqType::CALLBACK && cb) req_desc->callback = cb;
        _request_desc[req->rid()] = req_desc;
        return req_desc;
    }

    ReqDescribe::ptr getDesc(std::string& rid) {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _request_desc.find(rid);
        if (it != _request_desc.end()) return it->second;
        return ReqDescribe::ptr();
    }

    void delDesc(std::string& rid) {
        std::unique_lock<std::mutex> lock(_mutex);
        _request_desc.erase(rid);
    }

    std::mutex _mutex;
    std::unordered_map<std::string, ReqDescribe::ptr> _request_desc;
};
```

### 客户端：同步 + 异步 Future
```1:47:example/test/test1/rpc_client.cc
lcz_rpc::client::RpcClient client(true, "127.0.0.1", 8080);
Json::Value params;
params["num1"] = 66;
params["num2"] = 33;
Json::Value result;
client.call("add", params, result);
Json::Value async_params;
async_params["num1"] = 66;
async_params["num2"] = 3;
lcz_rpc::client::RpcCaller::RpcAsyncRespose future;
client.call("add", async_params, future);
```

更多示例位于 `example/`，涵盖消息分发、注册中心、Topic、Benchmark。

---

## 性能测试
`example/benchmark/benchmark_client.cc` 提供开箱即用的压测工具：
- 配置并发、请求数、同步/异步模式。
- 输出成功率、平均延迟、P50/P90/P99、QPS。
- 可切换负载均衡策略以对比性能。

运行示例（在 `rpc` 目录下）：
```bash
cd rpc
./build/example/benchmark/benchmark_client --host 127.0.0.1 --port 8889 \
  --concurrency 32 --requests 100000 --mode async
```

---

## 目录结构
```
RPC-/
├── autobuild/          # 构建脚本
├── rpc/                # RPC 框架源码
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── general/    # 抽象类、协议、工厂
│   │   ├── client/     # RpcClient、Requestor、TopicClient
│   │   ├── server/     # RpcServer、RegistryServer、TopicServer
│   │   └── muduo/      # muduo 子模块
│   ├── example/         # 示例程序
│   │   ├── test/       # RPC/Topic 示例
│   │   ├── despacher_* # 消息分发 & 注册中心示例
│   │   └── benchmark/  # 压测
│   └── build/          # CMake 输出
├── flowchat/           # 架构/流程图
└── README.md
```


---

## 参考资料
- 《从 0 实现分布式 RPC 框架的思考》系列：[460646015](https://zhuanlan.zhihu.com/p/460646015) / [33298916](https://zhuanlan.zhihu.com/p/33298916) / [388848964](https://zhuanlan.zhihu.com/p/388848964)
- `libjson-rpc-cpp`：JSON-RPC 框架，提供 stub 生成与多传输层支持 [GitHub](https://github.com/cinemast/libjson-rpc-cpp)
- `rest_rpc`：轻量级 C++ RPC 框架，API 设计与连接复用方案参考 [GitHub](https://github.com/qicosmos/rest_rpc)


