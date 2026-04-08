# 注册中心简单高可用 — 实现指南

本文档说明如何在不引入 Raft 的前提下，为注册中心做**简单高可用**：客户端支持多注册中心地址，连接失败或请求失败时自动切换下一个地址。实现完成后由我进行检查、验证与优化修改。

---

## 一、目标与范围

### 1.1 目标

- **客户端**（RpcClient、RpcServer 内的 ClientRegistry / ClientDiscover）支持配置**多个注册中心地址**。
- 使用**当前连接**发请求；若**无连接或连接不可用**，则按顺序尝试其他地址并建立连接，再重试请求。
- **不要求**：注册中心之间的数据同步、主备切换、强一致性（留给后续 Raft 方案）。

### 1.2 范围

- **只改客户端侧**：多地址配置 + 故障转移（选连接、重连、重试）。
- **不改**：`RegistryServer`、`PwithDManager`、注册中心协议与消息格式。

### 1.3 向后兼容

- 保留现有单地址 API：`(ip, port)` 或 `HostInfo(ip, port)` 的构造函数/参数。
- 新增多地址 API：`std::vector<HostInfo>` 或 `std::initializer_list<HostInfo>`，内部按列表顺序尝试连接与故障转移。

---

## 二、现状：单点连接点清单

以下位置目前都是**单地址 → 单连接**，需要改为「多地址 + 当前连接 + 故障转移」。

| 序号 | 文件 | 类 | 当前用法 | 说明 |
|------|------|-----|----------|------|
| 1 | `rpc_registry.hpp` | `ClientRegistry` | `ClientRegistry(ip, port)`，内部 `_client = ClientFactory::create(ip, port)` | Provider 侧：注册、上报负载、心跳 |
| 2 | `rpc_client.hpp` | `ClientDiscover` | `ClientDiscover(ip, port, cb)`，内部 `_client = ClientFactory::create(ip, port)` | Consumer 侧：服务发现、健康检查 |
| 3 | `rpc_client.hpp` | `RpcClient` | `RpcClient(enablediscover, ip, port)`，创建 `ClientDiscover(ip, port, ...)` | 对外 RPC 客户端，传单地址给 Discover |
| 4 | `rpc_server.hpp` | `RpcServer` | `RpcServer(access_addr, true, HostInfo(ip, port))`，创建 `ClientRegistry(ip, port)` | RPC 服务端，传单地址给 ClientRegistry |

**请求路径简述**：

- **ClientRegistry**：`methodRegistry` / `reportLoad` / `heartbeatProvider` 都先取 `_client->connection()`，若为空或未连接则直接返回 false，无重试。
- **ClientDiscover**：`serviceDiscover(method, detail, strategy)` 先取 `_client->connection()`，若为空或未连接则返回 false；健康检查定时器里同样用 `_client->connection()`。

因此实现要点是：**维护「注册中心地址列表」+「当前选中的连接」；在取连接时若无效则尝试其他地址并重连，必要时对当前请求重试一次**。

---

## 三、设计要点

### 3.1 多地址存储与顺序

- 使用 `std::vector<HostInfo> _registry_addrs` 保存注册中心地址，**顺序即尝试顺序**（第一个可视为“首选”）。
- 单地址构造（向后兼容）：构造时把 `(ip, port)` 或单个 `HostInfo` 转成 `vector` 只有一个元素。

### 3.2 当前连接与“当前下标”

- 保留一个「当前使用的」`BaseClient::ptr _client`，对应地址为 `_registry_addrs[_current_index]`（或单独记 `_current_index`）。
- 若希望简单实现，可以只存 `_client`，不显式存下标；需要切换时从列表头或从“下一个”开始依次尝试连接，成功则设为当前 `_client`。

### 3.3 何时触发“换地址”

- **取连接时**：若 `_client == nullptr` 或 `!_client->connection()` 或 `!conn->connected()`，则触发「选下一个地址并连接」。
- **请求返回失败时**（可选）：如 `methodRegistry` / `reportLoad` / `heartbeatProvider` / `serviceDiscover` 的 RPC 返回 false 且认为是连接级问题（例如超时、连接断开），可标记当前连接不可用并触发切换，下次取连接时会重连。  
  **建议第一版**：只在「取连接时发现不可用」则切换并重连，不做“请求失败即换地址”（可后续优化）。

### 3.4 连接建立策略

- **首次**：从 `_registry_addrs[0]` 开始，`ClientFactory::create(ip, port)` 并 `connect()`，成功则作为 `_client`。
- **切换**：当前连接不可用时，从下一项开始依次尝试 `create + connect`，直到有一个成功，更新 `_client`；若全部失败则返回 false，不无限重试。
- **可选**：避免每次请求都重试所有地址，可限制“每轮”最多尝试的地址个数（例如最多试完一整轮列表一次）。

### 3.5 线程安全

- `ClientRegistry` / `ClientDiscover` 可能被多线程使用（例如 RpcServer 的定时器线程 + 主线程），对 `_registry_addrs`、`_client`、`_current_index` 的读写需要加锁（如 `std::mutex`），与现有代码风格一致。

---

## 四、接口约定（便于你实现和我检查）

### 4.1 类型与常量

- 注册中心地址列表：`std::vector<HostInfo> registry_addrs`。  
- 若使用 `HostInfo`，需与现有 `HostInfo`（如 `std::pair<std::string, int>` 或项目内定义）一致。

### 4.2 ClientRegistry

- **现有**：`ClientRegistry(const std::string& ip, int port)`，保留不变。
- **新增**（二选一或都支持）：
  - `ClientRegistry(const std::vector<HostInfo>& registry_addrs)`
  - 或 `ClientRegistry(std::initializer_list<HostInfo> addrs)`
- **内部**：将单参数 `(ip, port)` 转为 `vector<HostInfo>{{ip, port}}`，与多地址走同一套逻辑。
- **方法**：`methodRegistry` / `reportLoad` / `heartbeatProvider` 在调用前：
  - 若当前无有效连接，则执行「选地址 + 连接」，成功后再发请求；
  - 若仍无有效连接，返回 false。

### 4.3 ClientDiscover

- **现有**：`ClientDiscover(const std::string& ip, int port, const OfflineCallback& cb)`，保留不变。
- **新增**：
  - `ClientDiscover(const std::vector<HostInfo>& registry_addrs, const OfflineCallback& cb)`
  - 或 `ClientDiscover(std::initializer_list<HostInfo> addrs, const OfflineCallback& cb)`
- **内部**：单地址构造转为单元素 vector，与多地址共用逻辑。
- **方法**：  
  - `serviceDiscover(method, detail_bylast, strategy)` / `serviceDiscover(method, host, strategy)`：  
    取连接时若无有效连接则先尝试「选地址 + 连接」，再发请求；仍失败则返回 false。  
  - 健康检查定时器回调里：若 `!_client || !_client->connection() || !conn->connected()`，可先尝试重连再刷新，避免一直打日志且不恢复。

### 4.4 RpcClient

- **现有**：`RpcClient(bool enablediscover, const std::string& ip, int port)`，保留。
- **新增**（多地址，仅当 `enablediscover == true` 时使用）：
  - `RpcClient(bool enablediscover, const std::vector<HostInfo>& registry_addrs)`  
  - 或 `RpcClient(bool enablediscover, std::initializer_list<HostInfo> addrs)`  
  内部创建 `ClientDiscover(registry_addrs, offlinecb)`。
- 单地址版可内部构造 `vector<HostInfo>{{ip, port}}` 再调多地址版，避免重复实现。

### 4.5 RpcServer

- **现有**：`RpcServer(const HostInfo& access_addr, bool enablediscover, const HostInfo& registry_server_addr)`，保留。
- **新增**（多地址）：
  - `RpcServer(const HostInfo& access_addr, bool enablediscover, const std::vector<HostInfo>& registry_addrs)`  
  当 `enablediscover == true` 时，用 `registry_addrs` 构造 `ClientRegistry`。
- 单地址版可把 `registry_server_addr` 转为单元素 vector 再调多地址版。

---

## 五、分步实现建议

按依赖关系实现，便于自测和后续我检查。

### 5.1 步骤 1：ClientRegistry 多地址 + 故障转移

**文件**：`src/client/rpc_registry.hpp`（及必要时的 `rpc_client.hpp` 中 ClientRegistry 使用处仅加构造方式，逻辑不改）。

1. 增加成员：
   - `std::vector<HostInfo> _registry_addrs`
   - `std::mutex _registry_mutex`（或复用现有锁，若已有）
   - 保留 `BaseClient::ptr _client`
2. 实现私有方法：`bool ensureConnection()` 或 `BaseConnection::ptr getConnection()`：
   - 若 `_client && _client->connection() && _client->connection()->connected()`，直接返回该 connection（或 true）。
   - 否则：从当前/首选地址开始，依次 `create(addr) + connect()`，第一个成功则赋给 `_client` 并返回 true/connection；全部失败返回 false。
3. 构造函数：
   - `ClientRegistry(ip, port)`：`_registry_addrs = {{ip, port}}`，可选调用一次 `ensureConnection()` 或延迟到首次请求。
   - `ClientRegistry(vector<HostInfo>)`：`_registry_addrs = 传入列表`，同上。
4. `methodRegistry` / `reportLoad` / `heartbeatProvider`：  
   开头先 `if (!ensureConnection()) return false;` 再 `auto conn = _client->connection();` 后续不变。

**自测**：单地址构造、多地址构造（列表第一个可用），行为与现有一致；多地址且第一个不可用、第二个可用时，应自动用第二个并成功。

---

### 5.2 步骤 2：ClientDiscover 多地址 + 故障转移

**文件**：`src/client/rpc_client.hpp`（ClientDiscover 在该文件中）。

1. 增加成员：`std::vector<HostInfo> _registry_addrs`、锁、保留 `_client`。
2. 实现 `ensureConnection()` 或 `getConnection()`，逻辑同 ClientRegistry：无有效连接则按顺序尝试连接，更新 `_client`。
3. 构造函数：
   - 单地址：`ClientDiscover(ip, port, cb)` → `_registry_addrs = {{ip, port}}`。
   - 多地址：`ClientDiscover(vector<HostInfo>, cb)`。
4. `serviceDiscover(method, detail_bylast, strategy)` 及 `serviceDiscover(method, host, strategy)`：  
   先 `ensureConnection()`，再取 `_client->connection()` 调 `_discover->serviceDiscover(conn, ...)`。
5. 健康检查定时器回调中：若当前无有效连接，先 `ensureConnection()`，再按现有逻辑刷新；避免在连接已断时一直不重连。

**自测**：单地址/多地址，第一个挂掉后第二个能接上并完成 serviceDiscover。

---

### 5.3 步骤 3：RpcClient 支持多地址构造

**文件**：`src/client/rpc_client.hpp`（RpcClient 类）。

1. 新增重载：  
   `RpcClient(bool enablediscover, const std::vector<HostInfo>& registry_addrs)`  
   当 `enablediscover == true` 时，`_discover_client = std::make_shared<ClientDiscover>(registry_addrs, offlinecb)`。
2. 原 `RpcClient(bool enablediscover, const std::string& ip, int port)` 可改为：  
   `RpcClient(enablediscover, std::vector<HostInfo>{{ip, port}})` 或内部构造单元素 vector 再调新重载，保证行为一致。

**自测**：`RpcClient(true, "127.0.0.1", 8080)` 行为不变；`RpcClient(true, {{"127.0.0.1", 8080}, {"127.0.0.1", 8081}})` 在 8080 不可用、8081 可用时能发现服务。

---

### 5.4 步骤 4：RpcServer 支持多地址构造

**文件**：`src/server/rpc_server.hpp`。

1. 新增重载：  
   `RpcServer(const HostInfo& access_addr, bool enablediscover, const std::vector<HostInfo>& registry_addrs)`  
   当 `enablediscover == true` 时，`_client_registry = std::make_shared<client::ClientRegistry>(registry_addrs)`。
2. 原三参构造保留，内部把 `registry_server_addr` 转为单元素 vector 再调新重载（推荐），或保留两份实现但保证逻辑一致。

**自测**：单地址行为不变；多地址且第一个挂、第二个存活时，注册/心跳/上报能切到第二个。

---

### 5.5 步骤 5：示例与配置（可选）

- 在 `example/test/test1/` 或你认为合适的地方，增加一个「多地址」示例：  
  例如 `RpcClient(true, {{"127.0.0.1", 8080}, {"127.0.0.1", 8081}})`，或从配置文件读两个端口。  
  不要求必须，但有助于我验证和回归。
- 若你有统一配置（如 json/环境变量），可在文档中说明「多地址」的配置格式，便于后续扩展。

---

## 六、验证清单（实现完成后由我检查与优化）

你可以先按下面自测，我检查时会在此基础上做代码审查和必要优化。

### 6.1 向后兼容

- [ ] 现有单地址调用方式不变：`ClientRegistry(ip, port)`、`ClientDiscover(ip, port, cb)`、`RpcClient(true, ip, port)`、`RpcServer(..., HostInfo(ip, port))`。
- [ ] 单实例注册中心、现有 example（如 test1 的 rpc_client / rpc_server / registry_server）无需改代码即可通过，行为与改前一致。

### 6.2 多地址与故障转移

- [ ] 配置两个注册中心地址，只启动第二个（或先启动第一个再 kill，只留第二个）：  
  - RpcClient 能发现服务；  
  - RpcServer 能注册、心跳、上报负载。
- [ ] 运行中 kill 当前连接的注册中心，再依赖另一个存活实例：  
  - 下一次请求或下一次定时心跳/上报/健康检查时，能自动切到另一个地址并成功（无需重启客户端/服务端进程）。

### 6.3 边界与健壮性

- [ ] 所有注册中心地址都不可用时，返回 false，不崩溃、不无限重试。
- [ ] 多线程调用（如 RpcServer 的 reportLoad/heartbeat + registerMethod）下无数据竞争，连接切换逻辑正确。

### 6.4 我会做的后续工作

- 代码审查：命名、锁粒度、重复代码抽取、与现有风格一致。
- 必要时的小优化：例如“请求失败时是否标记连接不可用并触发一次切换”、重试次数/退避、日志级别与内容。
- 若你已写多地址示例或配置说明，我会一起验证并给出改进建议。

---

## 七、可选优化（可不在第一版实现）

- **连接断开检测**：对 `_client->connection()->connected()` 的依赖若不足，可结合连接关闭回调把当前 `_client` 置为无效，下次自动走 `ensureConnection()`。
- **请求失败触发切换**：RPC 调用返回 false（如超时）时，将当前连接标记为不可用，下次取连接时换地址；可加“连续失败 N 次再换”避免偶发超时导致频繁换节点。
- **主备语义**：若你希望“优先用第一个地址，只有失败才用后面的”，当前设计（顺序尝试）已满足；无需额外主备协议。
- **健康检查间隔**：ClientDiscover 里已有定时刷新，多地址下在“无有效连接时先 ensureConnection 再刷新”即可；间隔可保持现有配置。

---

## 八、小结

| 项目 | 内容 |
|------|------|
| 目标 | 客户端多注册中心地址 + 无连接/连接不可用时自动换地址并重连 |
| 范围 | 仅客户端：ClientRegistry、ClientDiscover、RpcClient、RpcServer 的构造与连接使用方式 |
| 兼容 | 保留所有单地址 API，新增多地址重载或 initializer_list |
| 核心逻辑 | `ensureConnection()`：无有效连接则按列表顺序 create+connect，更新 _client |
| 验证 | 单地址行为不变；多地址下故障转移生效；全挂时返回 false 不崩溃 |

你按上述步骤实现即可；实现完成后告诉我，我会做检查、验证与优化修改。
