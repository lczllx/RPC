# 面试怎么讲这个 RPC 项目（按代码能讲圆）

下面每一节都是：**先背「面试先这样说」整段**（面试时够答一题）；**「追问再答」**用「问什么 → 答一句」；**「别讲错」**是容易说歪的事实，对照源码用。

路径默认从仓库里的 **`rpc/`** 目录说起（即 `rpc/src/...`）。

---

## 一、RPC 和你这个项目分别在干什么

**面试先这样说：**  
「RPC 就是远程过程调用：调用端把函数名、参数打成字节，通过网络发给对端，对端执行完再把结果打回来。它再封装也不像本地函数——会超时、会断线、不能传指针。我这个项目用 muduo 做 TCP，上面叠了一层自己定义的 LV 协议做定界，再用 jsoncpp 或 Protobuf 做 body 的序列化。注册中心、心跳、客户端选哪台机器，也是同一套连接和消息类型里做的。」

**追问再答：**  
- 问：参数怎么跨进程？→ 答：只能传序列化后的内容，Proto 里是 `RpcRequestEnvelope` 那种 `method + bytes`，JSON 就是 `RpcRequest` 序列化出来的字符串。  
- 问：和 HTTP 接口比？→ 答：HTTP 是资源+动词；RPC 更贴近「调一个过程」，我这儿是二进制帧 + 自己的 `MsgType`，不是 REST。

**别讲错：**  
- 底层发送入口是 `MuduoConnection::send`：先 `LVProtocol::serialize`，再 `TcpConnection::send`（`src/general/net.hpp`）。

---

## 二、一次调用从客户端到服务端，链路上发生什么

**面试先这样说：**  
「客户端业务代码构造好一条 `BaseMessage`，交给 `MuduoConnection::send`。里面用 `LVProtocol::serialize` 变成一串字节，通过 muduo 的 `TcpConnection` 发出去。服务端这边 muduo 读回调进到 **`MuduoServer::onMessage`**（客户端进程里则是 **`MuduoClient::onMessage`**），把 muduo 的 `Buffer` 包成我们自己的 `BaseBuffer`，在一个 `while` 循环里反复判断能不能解出一整帧 LV：能就 `LVProtocol::onMessage` 解出 `MsgType` 和 body，再交给 **`Dispacher::onMessage`**。Dispacher 根据 `MsgType` 找到注册好的回调；比如 RPC 服务端会进路由，按 **业务 method 名字** 找到具体处理函数；客户端收响应时，把 `RSP_RPC`、`RSP_RPC_PROTO` 等类型统一指到 **`Requestor::onResponse`**，用响应里的 **请求 id** 对上之前挂起的 `future` 或回调。」

**追问再答：**  
- 问：`Dispacher` 和「按 method 路由」是一层吗？→ 答：**不是一层**。Dispacher 只按 **`MsgType` 分大类**；JSON RPC 业务路由在 **`RpcRouter`**（`src/server/rpc_router.hpp`），按 method 字符串找 `ServiceDescribe`。  
- 问：同一根 TCP 上怎么又有 RPC 又有注册中心？→ 答：**靠不同 `MsgType`**，注册中心走 `REQ_SERVICE` / `RSP_SERVICE` 那条注册表里的 handler。

**别讲错：**  
- 服务端收包是 **`MuduoServer::onMessage`**，客户端收包是 **`MuduoClient::onMessage`**，不要只说 Server。  
- `Dispacher` 里如果 **`MsgType` 没注册**，会打错误日志并且 **`conn->shutdown()``**（`src/general/dispacher.hpp` 里 `find` 失败分支）。

---

## 三、LV 帧长什么样、怎么定界（必考）

**面试先这样说：**  
「线上字节顺序是：先 4 字节长度，再 4 字节消息类型，再 4 字节的 id 长度，接着 id 的二进制，最后是 body。注意：**第一个 4 字节里的数叫 `total_len`，它等于后面三块的长度之和**，也就是 `msg_type` 那 4 字节 + `id_len` 那 4 字节 + id 的字节数 + body 字节数，**不包含**最前面这个长度字段自己的 4 字节。所以一整帧占用的缓冲区长度是 **`4 + total_len`**。收的时候先用 `peekInt32` 偷看这个 `total_len`，如果当前可读长度还不到 `4 + total_len` 就等半包；够了再 `readInt32` 真正读掉并继续读类型、id、body。一次 TCP 回调里可能有多帧，所以外面套了 `while` 循环一直拆。」

**追问再答：**  
- 问：为什么用 peek？→ 答：**半包时不能 `read` 掉长度**，否则缓冲区错位，下一波数据对不齐。  
- 问：恶意连接一直发半包？→ 答：如果一直凑不齐完整帧、但缓冲区可读字节超过 **10MB**，代码里会 **`shutdown`**，防止内存被拖死（`MuduoServer::onMessage` / `MuduoClient::onMessage` 里和 `canProcessed` 配合的那段）。  
- 问：反序列化失败？→ 答：`LVProtocol::onMessage` 失败会关连接。

**别讲错：**  
- `canProcessed` 里变量名叫 `body_len`，**实际存的就是 `total_len`**，和 `serialize` 写的是同一个数；判断式是 **`body_len > readableSize() - 4`** 就还不完整（`src/general/net.hpp`）。

---

## 四、为什么要 `msg_type` 和请求 id（rid）

**面试先这样说：**  
「`msg_type` 告诉这一帧 body 该按哪种消息去反序列化、进哪个总回调：RPC、注册中心、Topic 都不一样。请求 id 用来在**客户端**把「发出去的多个请求」和「回来的多个响应」对上号：我维护一张 **`rid` → `ReqDescribe`** 的表，里面有 `promise`、可选的回调、以及 muduo 的定时器 id。响应到了先按 `rid` 查表，对上才 `set_value` 或调用户回调。」

**追问再答：**  
- 问：并发 RPC 怎么不乱？→ 答：**每个请求一个 `rid`**，响应带回同一个 `rid`，查表只唤醒对应那一个。  
- 问：`rid` 什么类型？→ 答：工程里是 **`std::string`**，当 `unordered_map` 的 key。

**别讲错：**  
- 工厂是 `MessageFactory::create(msgtype)` 再 `unserialize(data)`，在 `LVProtocol::onMessage` 里（`net.hpp`）。

---

## 五、JSON 和 Protobuf 两条路怎么分

**面试先这样说：**  
「协议上用不同的 **`MsgType`** 区分，比如 JSON RPC 和 Proto RPC 各有一套请求/响应类型。Proto 的 body 外面用 `RpcRequestEnvelope`：`method` 名字加 `bytes body` 装内层消息，定义在 `proto/rpc_envelope.proto`。JSON 那条走 jsoncpp，类型校验在 `RpcRouter` 里按字段描述做。压测里大包用 Proto，序列化和体积优势会明显，README 里有表格。」

**追问再答：**  
- 问：和 gRPC 比？→ 答：gRPC 基本是 **HTTP/2 + Proto + protoc 生成代码**；我这是 **裸 TCP + 手写 LV + 手写工厂**，栈薄、自己要管发现和超时传播。

**别讲错：**  
- 具体枚举数字以 `MsgType` 为准；`Dispacher` 打未知类型日志时带了一串说明（`dispacher.hpp` 里那行 `ELOG`）。

---

## 六、注册中心、心跳、客户端怎么选机器

**面试先这样说：**  
「注册中心进程里用 **`ProviderManager`** 维护：**方法名 → 多个 Provider 实例**。每个实例记连接、ip 端口、负载、以及 **`lastheartbeat` 时间戳**。Provider 启动时发 **`REQ_SERVICE` + REGISTER** 去登记；Consumer 发 **DISCOVER** 把列表拉回来，缓存在客户端的 **`MethodHost`** 里，真正发 RPC 前按策略从列表里挑一台。Provider 周期性发 **HEARTBEAT_PROVIDER** 刷新心跳时间。注册中心另外起一个 **`EventLoopThread`**，每隔几秒扫表：谁超过配置里的闲置时间就从集合里删掉，并 **`offlineNotify`** 通知发现侧。连接断开时也会走 **`onconnShoutdown`**：如果这条连接是 Provider，会对它注册过的每个 method 通知下线再删表。」

**追问再答：**  
- 问：心跳数字？→ 答：默认 **10 秒心跳、5 秒扫一次、15 秒没更新当掉线**，在 **`HeartbeatConfig`**（`src/general/publicconfig.hpp`）。  
- 问：为什么用 `steady_clock`？→ 答：**算间隔**用单调时钟，系统时间被 NTP 拨了也不会把存活算死。  
- 问：数据持久吗？→ 答：**不持久**，内存表；中心挂了要重新注册。

**别讲错：**  
- 扫描调用链：`RegistryServer` 里 `runEvery` → **`PwithDManager::sweepAndNotify`** → **`sweepExpired`** → 对每个过期 `(method, host)` **`offlineNotify`**（`src/server/rpc_registry.hpp`、`rpc_server.hpp`）。  
- 容器是 **`std::unordered_map<std::string, std::set<Provider::ptr>>`**，不是 vector。

---

## 七、负载均衡四种策略，用一句话讲清「怎么选」

**面试先这样说：**  
「都在客户端 **`MethodHost::selectHost`** 里实现。轮询用内部索引递增取模；随机用 `mt19937`；源哈希把字符串 `hash` 后模实例数，**如果 key 是空的就退化成随机**；最低负载先找出 **load 最小** 的所有机器，如果多台一样低，再在这批里做轮询，避免总打第一台。列表里如果同一个 host 再次出现，会 **更新它的 load**，避免一直看见旧负载。」

**追问再答：**  
- 问：这是一致性哈希吗？→ 答：**不是环**，就是 **hash 取模**；扩缩容会动模数，有迁移问题，面试可以说知道。  
- 问：最低负载没上报 load 呢？→ 答：大家都是默认 load，行为接近 **在全体里轮询**。

**别讲错：**  
- 代码在 **`src/client/rpc_registry.hpp`** 的 `MethodHost` 类里。

---

## 八、超时、晚到包、三种发请求方式（高频）

**面试先这样说：**  
「客户端用 **`Requestor`** 管在途请求。表里每个 `rid` 对应一条 **`ReqDescribe`**，里面有 **`timeout_triggered`** 标记。  
**走 `future` 的异步 `send`**：先发请求进表，再在连接所在 **`EventLoop` 上用 `runAfter` 挂定时器**，超时了调 **`onTimeout`**，给 `promise` 塞一个 **`rcode` 为 TIMEOUT** 的响应对象；正常返回先到会 **`cancel` 定时器** 再 `set_value`。  
**同步 `send`**：内部就是调异步版本，再 **`wait_for` 同样的超时时间**；如果超时，会先 **cancel 掉 muduo 定时器**，再手动调一次 **`onTimeout`**，避免定时器晚到又执行一遍。  
**纯回调的 `send`**：**没有注册 `runAfter`**，框架**不帮你做超时**；超时只能业务自己计时。  
如果响应晚到、但已经走过超时逻辑，**`timeout_triggered` 已为 true**，响应会被 **丢掉**，并从表里删掉，避免又成功又超时。」

**追问再答：**  
- 问：为什么 `onTimeout` 里 `set_value` 要 try/catch？→ 答：**防止和正常响应竞态双写 promise**。  
- 问：回调超时框架会调用户回调吗？→ 答：**不会**，代码里只打日志；异步超时会造一个 `RpcResponse` 或 `ProtoRpcResponse` 塞进 `future`。  
- 问：和 gRPC deadline 比？→ 答：gRPC 可以把截止时间传到服务端协作取消；我这边**主要是客户端本地计时**，协议里没有标配「服务端必须停算」。

**别讲错：**  
- 默认超时参数是 **`std::chrono::seconds(5)`**，在 `send(..., AsyncResponse&, timeout=...)` 的默认参数（`src/client/requestor.hpp`）。  
- 同步与定时器回调可能**不同线程**碰到同一 `rid`，所以表和标志位要加锁；细节可看 **`docs/timeout_management.md`**。

---

## 九、为什么 map 加一把锁、muduo 负责到哪一层

**面试先这样说：**  
「`Requestor` 里 **`unordered_map` + `mutex`**，所有增删查表、改 `timeout_triggered`、写 `timer_id` 都走这把锁，实现简单，并发高了锁会成为瓶颈，这是我知道的 trade-off。muduo 在我这儿负责 **TCP、缓冲区、epoll、定时器、多线程 accept**；**LV、消息含义、注册中心状态机**都是自己的代码。重业务不要长时间堵在 IO 回调里，否则同一条 loop 上其他连接都卡。」

**追问再答：**  
- 问：怎么优化锁？→ 答：**按 `rid` 分片**、或 **每连接一张在途表**（若 rid 只在连接内唯一）。  
- 问：跨线程关连接？→ 答：muduo 常规是 **`runInLoop`/`queueInLoop`** 丢回连接所在线程。

**别讲错：**  
- `MuduoServer` 构造里用了 **`TcpServer::kReusePort`**（`net.hpp`）。  
- Registry 心跳扫描在 **单独的 `EventLoopThread`**，和主 `TcpServer` 的 `loop` 不是同一个（`rpc_server.hpp`）。

---

## 十、Topic、gRPC、brpc（对比题怎么一句说完）

**面试先这样说：**  
「Topic 和 RPC **共用 LV 和 Dispacher 那套**，只是 `MsgType` 和 handler 不同；转发策略是一个枚举，例如广播、轮询、扇出等，proto 里 `TopicRequestEnvelope` 也有 `forward_strategy` 字段。和 **gRPC** 比：对方是 **HTTP/2 + 标准生态 + deadline/metadata**；我是 **TCP + 自研定界 + 自己写发现**。和 **brpc** 比：对方偏 **工业级一站式**（监控、策略多）；我偏 **把链路写明白**，观测和熔断限流很多还在 roadmap。」

**追问再答：**  
- 问：Topic 能替代 Kafka 吗？→ 答：**不能当消息队列**，没有 Kafka 那种持久化分区消费模型，更像在线通知。

**别讲错：**  
- 枚举名在 **`src/general/fields.hpp`** 的 `TopicForwardStrategy`。

---

## 十一、白板：注册 / 发现 / 心跳 / 掉线（按操作类型说）

**面试先这样说：**  
「对注册中心发的都是 **`REQ_SERVICE`**，用 **`ServiceOpType`** 区分干什么：**REGISTER** 写实例，**DISCOVER** 读列表，**LOAD_REPORT** 更新负载，**HEARTBEAT_PROVIDER** 刷新 **`lastheartbeat`**。Consumer 除了收推送，还会在 **`ClientDiscover` 里定时 `serviceDiscover`** 再拉一遍，防止只依赖推送时丢更新。掉线两条路：**TCP 断开**走 `onconnShoutdown`；**心跳过期**走定时扫描 `sweepExpired`，都会 **`offlineNotify`**，客户端 **`MethodHost::removeHost`** 把那一台从本地列表摘掉。」

**别讲错：**  
- `Provider::methodRegistry` 用的是 **`Requestor::send` 同步等注册结果**（`src/client/rpc_registry.hpp`）。

---

## 十二、附：打开哪个文件能对着讲（不用背行号）

| 要讲的内容 | 打开的文件 |
|------------|------------|
| LV 定界、10MB、while 拆帧 | `src/general/net.hpp` |
| 客户端超时、三种 send | `src/client/requestor.hpp` |
| 发现、负载、Provider 注册 | `src/client/rpc_registry.hpp` |
| 注册表、扫描、断连 | `src/server/rpc_registry.hpp` |
| Registry 起扫描线程 | `src/server/rpc_server.hpp` |
| 心跳默认秒数 | `src/general/publicconfig.hpp` |
| Dispacher、未知类型关连接 | `src/general/dispacher.hpp` |
| JSON 按 method 路由 | `src/server/rpc_router.hpp` |
| Proto envelope | `proto/rpc_envelope.proto` |
| 超时线程顺序长文 | `docs/timeout_management.md` |

---

## 使用方式（针对「讲不出来」）

1. 每节只背 **「面试先这样说」** 一段，背到能 **不看稿 30～45 秒说完**。  
2. 面试官追问时，扫一眼 **「追问再答」** 那一行，用同一句话答回去。  
3. **「别讲错」** 是纠错条：说之前心里过一遍，避免和源码矛盾。

若你希望 **把每一节「面试先这样说」再缩短成 15 秒版本**（电梯陈述），可以再说，我可以加在每一节标题下第二段「超短版」。
