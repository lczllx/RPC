# 为项目添加 Protobuf 序列化方案

本文档在阅读现有 JSON 序列化与消息体系后，给出添加 Protobuf 序列化的实现方案，供你实现时参考。

---

## 一、现状简要总结

### 1.1 序列化与消息层次

- **抽象层**（`abstract.hpp`）：`BaseMessage` 要求子类实现 `serialize()`、`unserialize(const std::string&)`、`check()`。
- **JSON 实现**（`message.hpp` + `detail.hpp`）：
  - `JsonMessage` 内部用 `Json::Value _data`，`serialize`/`unserialize` 通过 `JSON::serialize`/`JSON::deserialize` 完成。
  - 所有业务消息（`RpcRequest`、`RpcResponse`、`TopicRequest`、`TopicResponse`、`ServiceRequest`、`ServiceResponse`）都继承自 `JsonMessage`/`JsonRequest`/`JsonResponse`，通过 `_data` 的 key（如 `KEY_METHOD`、`KEY_PARAMS`、`KEY_RCODE`、`KEY_RESULT` 等）读写。
- **协议层**（`net.hpp`）：`LVProtocol` 只做「长度(4) + 类型(4) + id 长度(4) + id + body」的封装；**body 完全由消息自己序列化**：发的时候调 `msg->serialize()`，收的时候 `MessageFactory::create(msgtype)` 得到消息对象再 `msg->unserialize(data)`。因此**同一条连接上用 JSON 还是 Protobuf，只取决于创建的消息类型是“JSON 版”还是“Proto 版”**，协议帧本身不用改。

### 1.2 上层对消息类型的依赖

- **RpcCaller**（`caller.hpp`）：构造 `RpcRequest::ptr`，发完后把响应 `dynamic_pointer_cast<RpcResponse>`，并调用 `rpc_respmsg->result()`、`rpc_respmsg->rcode()`；**入参/出参统一是 `Json::Value`**（params、result）。
- **RpcRouter**（`rpc_router.hpp`）：`onrpcRequst` 收到 `RpcRequest::ptr`，用 `req->params()`、`req->method()`，再 `response(conn, req, Json::Value result, RespCode)`；**业务回调是 `ServiceCallback = function<void(const Json::Value&, Json::Value&)>`**。
- **Dispatcher**（`dispacher.hpp`）：按 `MsgType` 分发，用 `CallbackType<T>` 把 `BaseMessage::ptr` 转成 `T`（如 `RpcRequest::ptr`）再调用户回调。
- **MessageFactory**（`message.hpp`）：`create(MsgType)` 目前只创建 JSON 那一套具体类（如 `RpcRequest`、`RpcResponse`）。

结论：**若要支持 Protobuf 序列化，必须让“Proto 版 RPC 消息”也能被当作 `RpcRequest`/`RpcResponse` 使用**（即提供 `method()`、`params()`、`rcode()`、`result()` 等）。若只做兼容、不改 API，可保留 `Json::Value` 的 params/result，在边界做一次转换；**若要真正提升性能，必须提供“纯 Proto”API，让热路径零 JSON**（见下节）。

---

## 二、为何“复用 JSON”几乎没性能提升

若在 proto 里用 **string 存 JSON**（params/result 仍是 JSON 字符串），则：

- 线上传输的 body 里，真正占大头的 params/result 还是 JSON 文本，体积和解析成本与现在几乎一样。
- 服务端/客户端在 `params()`、`result()` 里还要做 **proto ↔ Json::Value** 的转换，多了一次序列化/反序列化，反而可能更慢。

所以要获得**实际性能提升**，至少要满足其一（或两者都做）：

1. **线上传的是二进制**：params/result 在 proto 里用 **bytes**（或嵌套 message）存**序列化后的 proto**，而不是 JSON 字符串；整条 RPC 消息的 body 都是 protobuf 二进制，解析快、体积小。
2. **热路径不走 JSON**：提供**基于 proto 的调用 API**（如 `call(method, const ReqProto& req, RespProto* resp)`），业务侧直接收发明文 proto，不再经过 `Json::Value`，零转换。

下面第三节给出「兼容路径 + 性能路径」的完整设计。

---

## 三、方案选型

### 方案 A：在现有 JsonMessage 里按 SerializationMethod 分支

- 在 `JsonMessage::serialize()`/`unserialize()` 里根据当前 `SerializationMethod` 选 JSON 或 Protobuf。
- 问题：当前所有字段都绑在 `Json::Value _data` 上，Protobuf 需要另一套数据结构（generated 的 C++ 类）；若在同一个类里同时维护 `_data` 和 proto 对象，双写/同步复杂，且违反单一职责。不推荐。

### 方案 B：双轨消息类型 + 统一接口（推荐）

- **保留现有 JSON 消息类不动**（如 `RpcRequest`/`RpcResponse` 继续用 `Json::Value` 实现）。
- **新增 Protobuf 消息类**：为 RPC（以及可选地 Topic/Service）定义 `.proto`，生成 C++ 代码；再实现“Proto 版”的请求/响应类，内部持有所生成的 proto 对象，实现 `BaseMessage::serialize()/unserialize()`（即 `SerializeAsString()`/`ParseFromString()`）。
- **统一接口**：为 RPC 引入抽象接口（如 `IRpcRequest`、`IRpcResponse`），提供 `method()`、`params()`、`rcode()`、`result()` 等。当前 JSON 版和新的 Proto 版都实现这些接口。**要获得性能提升**，Proto 版应在 proto 里用 **bytes** 存 params/result（如 Struct 序列化），仅在边界做一次 bytes↔Json::Value 转换；并可选提供纯 Proto API（见第四节）。
- **创建与协议**：`MessageFactory::create(MsgType)` 根据“当前序列化方式”（如全局配置或传入的 `SerializationMethod`）返回 JSON 实现或 Proto 实现；两者都向上转型为接口指针（或 `BaseMessage::ptr` + `dynamic_pointer_cast<IRpcRequest>` 等）。协议层仍用「长度+类型+id+body」，body 由各自消息的 `serialize()`/`unserialize()` 决定是 JSON 字符串还是 proto 二进制。

这样做的效果：

- 业务侧（Caller、Router、Dispatcher）只依赖接口和 `Json::Value`，不关心底层是 JSON 还是 Protobuf。
- 序列化方式可在连接或服务级别配置，通过 Factory 和协议层选用的消息类型体现。

---

## 四、真正获得性能提升的两条路径（必做其一）

在方案 B 的基础上，若要**有实质性能收益**，需要区分两条使用路径。

### 4.1 路径一：线上全 Proto 二进制 + 边界一次转换（兼容现有 Json API）

- **Wire 格式**：RPC body 整条是 proto。其中 **params / result 不用 string 存 JSON**，而是：
  - 用 **bytes** 存「某种二进制」：例如 **google.protobuf.Struct** 的序列化结果（与 JSON 可互转，但线上是二进制，更小、解析更快），或你自定义的通用 message（如 key-value 列表）的序列化。
  - 这样：**线上传输和解析都是 proto 二进制**，体积和 CPU 都比 JSON 好；只有在「业务仍用 Json::Value API」时，在**边界各做一次** Struct↔Json::Value 的转换（收包时 bytes → Struct → Json::Value 给 handler，回包时 Json::Value → Struct → bytes）。
- **收益**：相比现在「整条 body 都是 JSON」，能获得：更小 payload、更快解析；热路径若仍用 Json::Value，仅多一次边界转换，整体通常仍更快。
- **实现要点**：Proto 里 `bytes params = 3;` / `bytes result = 4;`，C++ 里用 `Struct`（或等价）与 `Json::Value` 互转；不要用 `string` 存 JSON。

### 4.2 路径二：纯 Proto API，热路径零 JSON（最大性能）

- **新 API**：在现有 `call(method, Json::Value params, Json::Value& result)` 之外，增加**基于类型的 proto 调用**，例如：
  - 客户端：`bool call_proto(const std::string& method, const google::protobuf::Message& req, google::protobuf::Message* resp);`  
    或模板：`template<typename Req, typename Resp> bool call_proto(const std::string& method, const Req& req, Resp* resp);`
  - 服务端：注册 handler 时支持「按 method + 请求/响应 proto 类型」分发，例如 `registerProtoHandler<ReqProto, RespProto>(method, callback)`，callback 签名为 `void(const ReqProto& req, RespProto* resp)`。
- **Wire**：请求/响应的 body 就是 ReqProto/RespProto 的 `SerializeAsString()`；或在外层再包一层 RpcRequestProto（method + id + bytes body），body = ReqProto 的序列化。**全程无 Json::Value**。
- **收益**：调用方和服务方都用强类型 proto，序列化/反序列化一次、无 JSON 解析、无边界转换，性能最优。

### 4.3 建议

- **最低限度**：采用 **4.1**——proto 里 params/result 用 **bytes + Struct（或等价）**，保证线上是二进制，仅在对 Json::Value API 的边界做一次转换；这样已有代码几乎不用改，就能获得带宽和解析上的提升。
- **若追求极致**：再增加 **4.2** 的 `call_proto` 与 proto 版 handler 注册，让新业务直接走 proto，老业务继续用 Json API。

---

## 五、推荐方案详细实现步骤

### 5.1 定义 .proto（兼顾性能：params/result 用 bytes，不用 JSON string）

- 在项目中新增目录，例如 `rpc/proto/`，放 `.proto` 文件。
- 先覆盖 **RPC**：
  - **RpcRequestProto**：例如 `string method=1; bytes params=2;`。**params 用 bytes**：可存 `google.protobuf.Struct` 的序列化（与 Json::Value 互转一次在边界完成），或你自定义的 GenericParams message 的序列化。**不要**用 `string params` 存 JSON。
  - **RpcResponseProto**：`int32 rcode=1; bytes result=2;`。**result 用 bytes**，同上，存 Struct 或自定义 message 的序列化。
  - 可选：id、msg_type 与 BaseMessage 对齐（或继续由外层 LV 带）。
- 用 `protoc` 生成 C++ 代码，并加入 CMake。

若要做**路径二（纯 Proto API）**：可再定义「按 method 区分的」请求/响应 message（或约定 body 直接是用户 Req/Resp proto 的 bytes），在 call_proto 与 registerProtoHandler 里专用。

### 5.2 抽象接口 IRpcRequest / IRpcResponse

- 新建头文件（如 `rpc_message_interface.hpp` 或放在 `message.hpp` 中）：
  - **IRpcRequest**：纯虚接口，继承 `BaseMessage`（或与 `BaseMessage` 多继承/组合，见下）。提供：
    - `virtual std::string method() const = 0;`
    - `virtual void setMethod(const std::string&) = 0;`
    - `virtual Json::Value params() const = 0;`
    - `virtual void setParams(const Json::Value&) = 0;`
    - 以及 `check()`、`serialize()`、`unserialize()`（若继承 BaseMessage 则已有声明）。
  - **IRpcResponse**：同理，`rcode()`、`setRcode()`、`result()`、`setResult()`，以及必要的 `check()`。
- 若接口不继承 `BaseMessage`，则 Dispatcher/Requestor 仍用 `BaseMessage::ptr`，在需要 RPC 语义时 `dynamic_pointer_cast<IRpcRequest>(msg)` / `dynamic_pointer_cast<IRpcResponse>(msg)`。若接口继承 `BaseMessage`，则需避免重复继承（例如让 `IRpcRequest` 不继承 BaseMessage，仅由具体类同时继承 JsonRequest/ProtobufMessage 与 IRpcRequest）。

### 5.3 现有 JSON 类实现接口

- 将当前 **RpcRequest** 改名为 **JsonRpcRequest**（或保留类名 `RpcRequest` 但让它实现 `IRpcRequest` 接口）。同样 **RpcResponse** 实现 **IRpcResponse**。
- 在 `MessageFactory::create(MsgType)` 中，当 `SerializationMethod::JSON` 时，对 `REQ_RPC`/`RSP_RPC` 仍创建当前的 Json 实现（并设置 `MsgType`）；返回类型可以是 `BaseMessage::ptr`，调用方用 `dynamic_pointer_cast<IRpcRequest>`/`IRpcResponse` 使用。

### 5.4 新增 Protobuf 消息类

- **基类**（可选）：可写一个 `ProtobufMessageBase : public BaseMessage`，内部持有 `google::protobuf::Message*`（或 `std::unique_ptr<google::protobuf::Message>`），在 `serialize()` 中调用 `Message::SerializeAsString()`，在 `unserialize()` 中 `ParseFromString()`；子类负责具体 proto 类型和 `check()`。
- **ProtoRpcRequest**：继承 `ProtobufMessageBase` 并实现 `IRpcRequest`。内部持有 `RpcRequestProto`；`method()`/`setMethod()` 直接读写 proto；**params**：proto 里是 `bytes params`。为实现 `params()`（返回 Json::Value），在**边界**将 bytes 反序列化为 Struct（或自定义 message）再转为 Json::Value；`setParams()` 则 Json::Value → Struct → bytes 写入。这样**线上始终是二进制**，只有走 Json API 时才做这一次转换。
- **ProtoRpcResponse**：同理，`result` 用 bytes 存 Struct（或等价）的序列化；`result()`/`setResult()` 在边界做 bytes ↔ Json::Value（经 Struct）的转换。

若实现**路径二**：可再提供「只操作 proto 的」请求/响应包装（或直接暴露 generated 类型），供 `call_proto` 与 proto handler 使用，热路径零 Json。

### 5.5 MessageFactory 与 SerializationMethod

- 在 `MessageFactory` 中增加对“当前序列化方式”的感知：
  - 方式 1：`MessageFactory::setSerializationMethod(SerializationMethod)` 静态/全局配置；
  - 方式 2：`create(MsgType, SerializationMethod method)` 重载。
- 对 `MsgType::REQ_RPC` / `RSP_RPC`：
  - 若为 `JSON`，创建现有 Json 版（如 `JsonRpcRequest`/`JsonRpcResponse` 或你命名的类）；
  - 若为 `PROTOBUF`，创建 `ProtoRpcRequest`/`ProtoRpcResponse`。
- 其他 `MsgType`（REQ_TOPIC、RSP_TOPIC、REQ_SERVICE、RSP_SERVICE）第一阶段可仍只创建 JSON 版；后续若要支持 Proto，按同样模式加 ITopicRequest/IServiceRequest 等接口和 Proto 实现即可。

### 5.6 协议层与连接

- **LVProtocol** 无需改格式；它只调 `msg->serialize()` 和 `msg->unserialize(data)`。只要 Factory 根据 SerializationMethod 创建的是 Proto 版消息，线上传输的 body 自然就是 Protobuf 二进制。
- 若希望**同一连接上同时支持 JSON 与 Protobuf**（不推荐首版做）：可在 LV 帧中增加 1 字节“序列化类型”，在 `onMessage` 里根据该字节选择 `MessageFactory` 的创建分支；且需要约定何时发送哪种类型（例如通过首包或握手）。首版更简单的是：**每连接或每服务只使用一种序列化方式**，由 Server/Client 构造时选定 SerializationMethod，并传给 Factory 或全局配置。

### 5.7 上层调用处改动

- **RpcCaller**：将 `dynamic_pointer_cast<RpcResponse>` 改为 `dynamic_pointer_cast<IRpcResponse>`（或保留对 `RpcResponse` 的 cast，若你让 `RpcResponse` 成为接口的“别名”或基类）；调用 `rcode()`、`result()` 不变，仍得到 `Json::Value`。
- **RpcRouter**：`onrpcRequst` 参数改为 `IRpcRequest::ptr`（或通过接口取 method/params）；`response(conn, req, result, rcode)` 里构造响应时，通过 Factory 创建“当前序列化方式”的响应类型（IRpcResponse 实现），再 setRcode/setResult 并发送。
- **Dispatcher**：注册时使用 `registerhandler<IRpcRequest>(MsgType::REQ_RPC, ...)`（或你统一的请求接口类型），这样无论收到的是 Json 还是 Proto 实现，都能正确转成接口指针并调用。

### 5.8 构建与依赖

- CMake：在 `rpc` 或 `rpc/src` 中 `find_package(Protobuf REQUIRED)`，用 `protobuf_generate_cpp` 或手动添加生成的 `.pb.cc`，并把生成目录加入 `include_directories`；`target_link_libraries(lcz_rpc INTERFACE ... protobuf::libprotobuf)`。
- 确保编译顺序：proto 先生成，再编译依赖 generated 头文件的 Protobuf 消息类。

---

## 六、涉及文件与改动清单（按推荐方案）

| 位置 | 改动 |
|------|------|
| **新增** `rpc/proto/*.proto` | 定义 RpcRequestProto、RpcResponseProto（及可选 id/msgtype） |
| **新增** generated `*.pb.cc/h` | protoc 生成，加入工程 |
| **新增** `rpc_message_interface.hpp`（或合并到 message.hpp） | 定义 IRpcRequest、IRpcResponse |
| **修改** `message.hpp` | 现有 RpcRequest/RpcResponse 实现接口；或改名为 JsonRpcRequest/JsonRpcResponse 并实现接口；新增 ProtoRpcRequest、ProtoRpcResponse（或单独 proto_message.hpp） |
| **修改** `MessageFactory` | 根据 SerializationMethod 创建 Json 或 Proto 实现 |
| **修改** `fields.hpp` | 已有 `SerializationMethod::PROTOBUF`，如需可加“默认序列化方式”配置 |
| **修改** `caller.hpp` | 使用 IRpcResponse 接口取 result/rcode（或保持 RpcResponse 为接口基类名） |
| **修改** `rpc_router.hpp` | 使用 IRpcRequest/IRpcResponse，构造响应时用 Factory 创建当前序列化类型的响应 |
| **修改** `rpc_server.hpp` | 若需，在创建 Server 时设置 SerializationMethod（或从配置读） |
| **修改** `net.hpp`（可选） | MuduoServer/MuduoClient 若需要按连接或全局传入 SerializationMethod，可在这里把 method 传给 Factory 或保存为成员；否则用全局/单例配置即可 |
| **修改** `CMakeLists.txt` | 增加 Protobuf 查找、proto 生成、链接 libprotobuf |

---

## 七、可选扩展（后续再做）

- **Topic / Service 的 Protobuf**：为 TopicRequest/Response、ServiceRequest/Response 定义 proto，并实现 ITopicRequest、IServiceRequest 等接口及 Proto 版实现，Factory 中按 MsgType + SerializationMethod 分支。
- **同一连接双格式**：在 LV 帧中增加 1 字节序列化类型，onMessage 时根据该字节选择反序列化路径（需明确握手或首包约定）。
- **性能与兼容**：若 result/params 体量大，可考虑 proto 中直接使用结构化 message 而非 JSON 字符串，接口层再提供 proto ↔ Json::Value 的转换工具，供需要 JSON API 的调用方使用。

---

## 八、小结

- **协议层**（LV）不用改；body 由消息自己的 `serialize()`/`unserialize()` 决定是 JSON 还是 Protobuf。
- **消息层**：保留现有 JSON 消息，新增 Proto 版消息；两者通过 **IRpcRequest/IRpcResponse** 统一对外 API；Proto 版在**边界**做 bytes（Struct）↔ Json::Value 转换，**不在 proto 里用 string 存 JSON**，这样线上才是二进制，才有解析和带宽收益。
- **性能**：要真正提升性能，必须（1）proto 中 params/result 用 **bytes**（如 Struct 序列化），和/或（2）提供 **call_proto / registerProtoHandler** 等纯 Proto API，热路径零 JSON。
- **创建与配置**：MessageFactory 根据 **SerializationMethod** 创建 Json 或 Proto 实现；Server/Client 在启动或建连时设定序列化方式即可。
