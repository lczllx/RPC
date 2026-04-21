# 注册中心基于 etcd 的高可用 — 设计实现文档

本文档描述在**不改变对外 RPC 注册协议**（仍使用现有 TCP + `ServiceRequest` / `ServiceResponse`）的前提下，将注册发现状态从进程内存迁移到 **etcd**，使注册中心进程可水平扩展、元数据集群高可用的**设计与分阶段实现路线**。

**与现有文档的关系**：

- `registry_ha_simple_impl_guide.md`：客户端多注册中心地址 + 故障转移，**不解决**多实例间数据一致。
- 本文档：以 **etcd 为状态真源**，解决多 `RegistryServer` 实例下的**一致性与自动下线**，可与多地址方案叠加使用。

---

## 一、背景与目标

### 1.1 现状（代码语义摘要）

- `RegistryServer`（`rpc_server.hpp`）挂载 `PwithDManager`（`rpc_registry.hpp`），处理 `MsgType::REQ_SERVICE`。
- `ProviderManager` 在内存中维护 `method -> providers`、`conn -> provider`，依赖连接与定时扫描（心跳/空闲超时）做存活判断。
- Provider 侧通过 `ClientRegistry`（`client/rpc_registry.hpp`）与注册中心通信。

**单实例瓶颈**：内存状态无法在多副本间共享；多实例部署时会出现「各实例视图不一致」或「必须粘性会话」等问题。

### 1.2 目标

| 维度 | 目标 |
|------|------|
| 一致性 | 全集群对「某 method 下有哪些实例」有统一视图；以 etcd 为准。 |
| 高可用 | etcd 使用 3/5 成员集群；`RegistryServer` 可多实例 + 前置负载均衡。 |
| 生命周期 | 实例上下线可靠：网络分区、进程崩溃能自动摘除（lease 过期）。 |
| 兼容 | 优先保持 Provider/Consumer **仍只连注册中心 TCP**，不强制改客户端协议。 |

### 1.3 非目标（可显式排除）

- 不在本文档内规定具体 C++ etcd 客户端选型（`etcd-cpp-apiv3` / `grpc` 手写等由实现阶段决定）。
- 不替代你其他子系统中的 **自研 Raft**；Raft 与 etcd 职责边界见「第八章」。

---

## 二、总体架构

```
  Provider / Consumer
          │
          │  现有 TCP + ServiceRequest（不变）
          ▼
  ┌───────────────────────────────────────┐
  │  RegistryServer（可多实例，LB 入口）   │
  │  PwithDManager：协议适配 / 鉴权 / 缓存 │
  └───────────────────┬───────────────────┘
                      │  etcd API（Put/Get/Lease/KeepAlive/Watch）
                      ▼
              ┌───────────────┐
              │  etcd cluster │
              │   (3 或 5 节点) │
              └───────────────┘
```

**核心原则**：

1. **etcd 为唯一真源**：`DISCOVER` 的权威结果来自 etcd（或经短 TTL 缓存的 etcd 视图）。
2. **注册中心无状态化**：任意 `RegistryServer` 实例处理同一请求应等价；不在单机内存中「唯一持有」注册表。
3. **实例生命周期与 TCP 会话解耦**：是否存活以 **lease** 与 **显式注销** 为准，避免「连接挂在实例 A、状态却在 etcd」时用 TCP 断连作为唯一下线信号。

---

## 三、etcd 数据模型

### 3.1 Key 空间约定

建议使用固定版本前缀，便于迁移与批量清理：

| 类型 | Key 模式 | 说明 |
|------|-----------|------|
| 服务实例 | `/lcz-rpc/v1/svc/{method}/{instance_id}` | `method` 需约定字符集（如 URL 安全转义） |
| （可选）负载旁路 | `/lcz-rpc/v1/load/{method}/{instance_id}` | 高频写负载时可与主实例元数据分离，降低同 key 写冲突 |

`instance_id`：建议 **UUID**，或经规范化且不含 `/` 的 `host-port`；实现上必须保证同一物理进程重连时可 **幂等更新** 或 **复用同一 instance_id**（见 5.2）。

### 3.2 Value 内容

JSON 或 protobuf 二选一，团队统一即可。最小字段建议：

- `host`（string）、`port`（int）
- `load`（int，可选）
- `registered_at` / `version`（可选，便于调试与乐观并发）

### 3.3 Lease（租约）与心跳

- Provider 注册时：`Grant` lease → `Put` 绑定 `lease_id`。
- 心跳：`LeaseKeepAlive` 按固定周期发送；周期应满足 **TTL > k × 心跳间隔**（如 k≥2），避免抖动导致误删。
- 负载上报：可 **同 key Put** 更新 value（仍绑定同一 lease），或写入独立 load key（见 3.1）。

### 3.4 发现与变更通知

- **同步发现**：`GET` 前缀 `/lcz-rpc/v1/svc/{method}/`。
- **增量更新**（若保留 `onlineNotify` 等推送语义）：对同一前缀 **Watch**，将事件映射为现有通知回调；注意 **历史版本与压缩** 下的 watch 重建策略。

---

## 四、组件职责划分

### 4.1 RegistryServer / PwithDManager

| 当前行为 | 迁移后职责 |
|----------|------------|
| `REGISTER` 写内存 | 申请/续用 lease，`Put` 实例 key；返回成功需 etcd 提交成功 |
| 心跳扫描内存超时 | 以 lease 为主；可删除或弱化全表扫描，避免与 lease 双轨 TTL |
| `DISCOVER` 读内存 | 前缀 `GET` etcd；可选本地缓存 + watch 更新 |
| `reportLoad` | 更新 etcd 中 value 或 load key |
| 连接关闭 `delProvider` | **补充**：若业务要求「断连立即下线」，可 `DELETE` 或 `Revoke`；**不能**作为唯一下线路径 |

### 4.2 Provider（RpcServer + ClientRegistry）

- 协议可不变：仍发 `REGISTER` / 心跳 / 负载；由注册中心写 etcd。
- **可选演进**：Provider 直写 etcd（减少一跳），但会扩大安全面与客户端依赖，**第一版不建议**。

### 4.3 Consumer（ClientDiscover）

- 协议可不变：仍向注册中心 `DISCOVER`，由注册中心读 etcd。
- **可选演进**：Consumer 直读 etcd + watch，注册中心仅作兼容网关；**第二版或大规模 QPS 时**再评估。

---

## 五、关键设计决策

### 5.1 幂等与重复注册

- 同一 `method + host + port` 再次 `REGISTER`：应 **合并为同一逻辑实例**（相同 `instance_id` 或先 `GET` 再决定），避免 watch 端看到无意义重复增删。
- 注册中心崩溃重启：Provider 重连重注册应 **覆盖或续租** 原 key，不产生僵尸双份。

### 5.2 TCP 与 etcd 状态的关系

推荐策略：

- **以 etcd lease 为准**判定实例是否在线。
- TCP 断开：若实现简单，可 **best-effort** `DELETE`/`Revoke`；若多副本下连接与请求不一定同机，**不能**依赖「只有持有连接的节点才能删 key」。

### 5.3 缓存与一致性

- 注册中心可对热点 `method` 做 **短 TTL 内存缓存**（例如数百毫秒～数秒），降低 etcd QPS。
- 强一致读路径：关键发现可 **直连 etcd 线性读**（视客户端 API 支持而定）；缓存需定义失效条件（watch 或 TTL）。

### 5.4 与「客户端多注册中心地址」方案的关系

- **多地址**：解决「连哪台 Registry」的入口冗余。
- **etcd**：解决「Registry 之间数据一致」与「实例自动下线」。

两者互补：生产上常见组合为 **etcd 集群 + 多 Registry + 客户端多地址**。

---

## 六、分阶段实现路线

### 阶段 0：前置

- 部署 **单节点 etcd**（开发）与 **三节点集群**（联调/生产规格）。
- 确定 key/value 规范、字符转义、最大 key 数量与单 method 实例上限（防滥用）。

### 阶段 1：单 Registry + etcd，双写校验

- `REGISTER` / `DISCOVER` / 负载 / 心跳：**同时写读内存与 etcd**。
- 对比两者结果，记录差异日志；**对线上行为仍以内存为准**（降低风险）。

### 阶段 2：以 etcd 为读源

- `DISCOVER` **只读 etcd**（内存可作对照，不一致打日志）。
- 保留内存写入或逐步只写 etcd（按模块切）。

### 阶段 3：以 etcd 为写源与生命周期真源

- `REGISTER`、心跳、负载：**仅 etcd**；内存仅保留连接相关结构（若仍需要）或完全移除 `ProviderManager` 中的 method 索引。
- 删除与 lease 重复的「空闲扫描」逻辑，或明确只保留非 etcd 的辅助功能。

### 阶段 4：多 Registry 实例 + 负载均衡

- 前置 **四层 LB**；各实例配置相同 etcd endpoints。
- 压测：并发注册、watch 数量、前缀列举延迟；必要时加缓存与连接池。

### 阶段 5：运维与韧性

- 启用 **TLS**、认证；备份与恢复演练；监控 **leader 切换、磁盘、proposal 延迟**。
- 文档化 **etcd 版本升级** 与 **key 迁移**（前缀版本 bump）。

---

## 七、测试与验收清单

| 类别 | 场景 |
|------|------|
| 功能 | 注册、发现、负载更新、重复注册幂等 |
| 生命周期 | lease 过期后 key 消失，发现列表更新 |
| 故障 | Registry 单实例挂掉，其他实例仍可发现；Provider 重连后状态正确 |
| 故障 | etcd 成员挂 1 台，集群仍可读可写（3 节点场景） |
| 压力 | 大量 method、大量 watch、前缀 GET QPS |
| 兼容 | 现有 demo / example 无需改启动参数即可跑通（若保持协议不变） |

---

## 八、与自研 Raft 的边界说明（简历与评审口径）

- **自研 Raft**：适用于本项目内**自定义复制状态机**、配置、或强绑定业务语义的模块。
- **etcd**：工业界成熟的 **键值存储 + 租约 + 监视**，适合**服务注册发现**、配置分发等标准模式。

避免给人「两个 Raft 重复实现」的印象：应表述为 **不同层次/不同子系统** 的技术选型，而非同一数据的重复复制。

---

## 九、风险与缓解

| 风险 | 缓解 |
|------|------|
| etcd 成为热点 | 前缀缓存、合并 watch、控制轮询频率 |
| watch 在压缩后失效 | 实现 watch 重建与版本号处理 |
| 双轨 TTL（内存扫描 + lease）不一致 | 阶段 3 后只保留 lease |
| key 爆炸 | 配额、TTL、method 命名规范、治理 |

---

## 十、文档维护

- **版本**：与仓库实现迭代时同步更新「阶段完成情况」与 key 前缀版本。
- **关联代码入口**：`rpc/src/server/rpc_registry.hpp`（`PwithDManager`、`ProviderManager`、`DiscoverManager`）、`rpc/src/server/rpc_server.hpp`（`RegistryServer`）、`rpc/src/client/rpc_registry.hpp`（`ClientRegistry`）。

---

## 修订记录

| 日期 | 说明 |
|------|------|
| 2026-04-17 | 初稿：基于「Registry 薄适配层 + etcd 真源 + 分阶段迁移」路线整理 |
