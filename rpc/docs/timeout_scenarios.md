# 超时触发场景详解

本文档说明什么情况下会触发超时，以及超时后的行为。

---

## 一、超时的本质

**超时 = 客户端发送请求后，在设定的时间内没有收到服务端的响应**

- **不是**：客户端“在一定时间内没有响应”（客户端是主动发请求的一方）
- **而是**：客户端“在一定时间内没有收到服务端的响应”

---

## 二、超时触发的条件

### 2.1 时间条件

- **默认超时时间**：5 秒（`std::chrono::seconds(5)`）
- **可自定义**：调用 `send(conn, req, resp, timeout)` 时传入自定义的 `timeout` 参数
- **计时起点**：从 `conn->send(req)` 发送请求后开始计时
- **计时终点**：收到响应（`onResponse` 被调用）或超时时间到

### 2.2 触发路径

超时可以通过两种路径触发（任一触发即生效）：

1. **muduo 定时器到期**（EventLoop 线程）
   - 发送请求时设置了 `runAfter(timeout_sec, onTimeout)`
   - 时间到后，EventLoop 线程执行 `onTimeout(req_id)`

2. **同步 send 的 wait_for 超时**（用户线程）
   - 同步调用时，用户线程阻塞在 `async_resp.wait_for(timeout)`
   - 如果超时时间内没有收到响应，`wait_for` 返回 `timeout`，用户线程主动调用 `onTimeout(req_id)`

---

## 三、什么场景会触发超时

### 3.1 网络问题

| 场景 | 说明 | 示例 |
|------|------|------|
| **网络延迟高** | 请求/响应在网络中传输时间过长 | 跨地域调用、网络拥塞 |
| **网络丢包** | 请求或响应在传输过程中丢失 | 需要重传，但重传时间超过超时时间 |
| **网络中断** | 客户端与服务端之间的网络连接断开 | 网线断开、WiFi 断开、中间路由器故障 |
| **防火墙/代理超时** | 中间设备设置了更短的超时时间 | 代理服务器 3 秒超时，但客户端设置了 5 秒 |

### 3.2 服务端问题

| 场景 | 说明 | 示例 |
|------|------|------|
| **服务端处理慢** | 服务端业务逻辑执行时间超过超时时间 | 数据库查询慢、复杂计算、等待外部资源 |
| **服务端崩溃/重启** | 服务端进程异常退出，无法响应 | 段错误、OOM、手动 kill |
| **服务端卡死** | 服务端进程还在，但不处理请求 | 死锁、无限循环、阻塞在某个 I/O |
| **服务端负载过高** | 请求排队时间过长 | 大量并发请求，请求在队列中等待超过超时时间 |
| **服务端不响应** | 服务端收到请求但不返回响应 | bug 导致忘记发送响应、异常被吞掉 |

### 3.3 连接问题

| 场景 | 说明 | 示例 |
|------|------|------|
| **连接已断开** | TCP 连接在发送请求后断开 | 服务端主动关闭连接、网络中断导致连接断开 |
| **连接未建立** | 尝试发送请求时连接还未建立成功 | 连接建立失败、连接建立超时 |

### 3.4 实际测试场景（我们之前测试过的）

**场景：服务端处理慢**

```cpp
// slow_rpc_server.cc - 服务端 sleep 10 秒才处理
void add_slow(const Json::Value &req, Json::Value &resp) {
    std::this_thread::sleep_for(std::chrono::seconds(10));  // 故意延迟 10 秒
    int num1 = req["num1"].asInt();
    int num2 = req["num2"].asInt();
    resp = num1 + num2;
}
```

- **客户端超时设置**：5 秒（默认）
- **结果**：客户端在 5 秒后触发超时，返回失败；服务端在 10 秒后处理完，但响应会被客户端忽略（因为已经超时）

---

## 四、超时后的行为

### 4.1 超时处理流程

```
超时触发（onTimeout）
  │
  ├─► 检查 req_desc 是否存在（可能已被 onResponse 清理）
  │     若不存在 → 直接 return（幂等）
  │
  ├─► 检查 timeout_triggered 标志
  │     若已为 true → return（防止重复处理）
  │     否则 → 设置 timeout_triggered = true
  │
  ├─► 根据请求类型处理：
  │     ASYNC（future 模式）：
  │       • 创建 timeout_msg（rcode = TIMEOUT）
  │       • try { promise.set_value(timeout_msg) }
  │       • catch { ... }  // 若 promise 已被 onResponse 设置，忽略
  │     
  │     CALLBACK（回调模式）：
  │       • 只打日志，不调用用户回调
  │
  └─► delDesc(req_id)  // 清理请求描述，释放资源
```

### 4.2 对调用方的影响

**同步调用**（`send(conn, req, resp, timeout)`）：

- 超时后：`wait_for` 返回 `timeout` → `onTimeout` 被调用 → `return false`
- 调用方收到：`send()` 返回 `false`，`resp` 可能未设置或包含超时响应（取决于实现细节）

**异步调用**（`send(conn, req, async_resp, timeout)`）：

- 超时后：定时器触发 `onTimeout` → `promise.set_value(timeout_msg)` → future 变为 ready
- 调用方收到：`async_resp.get()` 返回一个 `RpcResponse`，其 `rcode()` 为 `RespCode::TIMEOUT`

**回调调用**（`send(conn, req, cb)`）：

- **注意**：回调模式**没有超时机制**，不会触发超时
- 如果服务端一直不响应，回调永远不会被调用

---

## 五、超时时间设置建议

### 5.1 默认值

- **当前默认**：5 秒
- **适用场景**：局域网内、服务端处理快的场景

### 5.2 如何自定义

```cpp
// 同步调用，设置 10 秒超时
BaseMessage::ptr resp;
bool ret = requestor->send(conn, req, resp, std::chrono::seconds(10));

// 异步调用，设置 3 秒超时
AsyncResponse async_resp;
requestor->send(conn, req, async_resp, std::chrono::milliseconds(3000));
```

### 5.3 设置建议

| 场景 | 建议超时时间 | 原因 |
|------|------------|------|
| **本地/同机房调用** | 1-3 秒 | 网络延迟低，服务端处理快 |
| **跨地域调用** | 5-10 秒 | 网络延迟较高 |
| **复杂计算/数据库查询** | 10-30 秒 | 业务逻辑本身耗时 |
| **批量操作** | 30-60 秒 | 处理大量数据需要时间 |
| **实时性要求高** | 1 秒以内 | 快速失败，避免阻塞 |

---

## 六、超时 vs 其他错误

### 6.1 超时（TIMEOUT）

- **触发条件**：在超时时间内没有收到响应
- **响应码**：`RespCode::TIMEOUT`
- **可能原因**：网络问题、服务端慢、服务端不响应

### 6.2 连接失败（CONNECTION_CLOSED）

- **触发条件**：连接断开、连接未建立
- **响应码**：`RespCode::CONNECTION_CLOSED`（如果实现中有）
- **与超时的区别**：连接失败是“无法发送请求”，超时是“发送了请求但没收到响应”

### 6.3 服务不存在（SERVICE_NOT_FOUND）

- **触发条件**：服务端收到请求，但找不到对应的服务方法
- **响应码**：`RespCode::SERVICE_NOT_FOUND`
- **与超时的区别**：服务端会返回响应（错误响应），不会超时

---

## 七、实际测试示例

### 7.1 测试：服务端处理慢导致超时

```cpp
// 服务端：slow_rpc_server.cc
void add_slow(const Json::Value &req, Json::Value &resp) {
    std::this_thread::sleep_for(std::chrono::seconds(10));  // 延迟 10 秒
    resp = req["num1"].asInt() + req["num2"].asInt();
}

// 客户端：timeout_test_client.cc（默认 5 秒超时）
RpcClient client(true, "127.0.0.1", 8080);
bool ret = client.call("add", params, result);
// 结果：5 秒后超时，ret = false，日志显示 "请求超时"
```

### 7.2 测试：服务端崩溃导致超时

```bash
# 1. 启动服务端
./test1_rpc_server &

# 2. 启动客户端（会发送请求）
./test1_rpc_client &
# 此时请求已发送，等待响应

# 3. 立即 kill 服务端
killall test1_rpc_server

# 结果：客户端在 5 秒后超时（因为服务端已死，无法响应）
```

### 7.3 测试：网络断开导致超时

```bash
# 1. 客户端和服务端在不同机器上
# 2. 客户端发送请求
# 3. 断开网络（拔网线、禁用网卡）
# 结果：客户端在 5 秒后超时（网络中断，无法收到响应）
```

---

## 八、总结

| 问题 | 答案 |
|------|------|
| **什么时候会超时？** | 客户端发送请求后，在设定的时间内（默认 5 秒）没有收到服务端响应 |
| **什么场景会触发超时？** | 网络问题（延迟、丢包、中断）、服务端问题（慢、崩溃、卡死）、连接问题（断开、未建立） |
| **超时后会发生什么？** | `onTimeout` 被调用 → 设置 `timeout_triggered` → 对 ASYNC 模式设置超时响应 → 清理请求描述 → 调用方收到失败或超时响应 |
| **如何避免超时？** | 1. 增加超时时间（适合业务本身耗时）<br>2. 优化服务端性能（减少处理时间）<br>3. 优化网络（减少延迟）<br>4. 使用异步/回调模式（不阻塞，但回调模式没有超时保护） |
| **超时是错误吗？** | 不一定。可能是网络问题、服务端问题，也可能是业务本身需要较长时间。调用方应该根据业务需求设置合适的超时时间，并处理超时情况。 |

---

## 九、代码中的超时检查点

在代码中，超时检查发生在以下位置：

1. **发送请求时**：`send(conn, req, async_resp, timeout)` → 设置 `runAfter(timeout_sec, onTimeout)`
2. **同步等待时**：`async_resp.wait_for(timeout)` → 如果超时，主动调用 `onTimeout`
3. **定时器到期时**：EventLoop 执行 `onTimeout(req_id)` → 处理超时逻辑
4. **收到响应时**：`onResponse` → 检查 `timeout_triggered`，如果已超时则忽略响应

这样设计的好处是：**无论响应先到还是超时先到，都能正确处理，不会重复处理或丢失结果**。
