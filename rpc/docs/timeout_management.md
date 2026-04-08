# Requestor 超时管理梳理

本文档按「谁在什么线程调谁、为什么」梳理 `requestor.hpp` 里的超时逻辑。

---

## 一、参与的角色与数据结构

### 1.1 请求描述 `ReqDescribe`

每个发出的请求对应一个 `ReqDescribe`，保存在 `_request_desc[req_id]` 里：

| 字段 | 含义 |
|------|------|
| `reqtype` | ASYNC（future）/ CALLBACK（回调） |
| `response` | `std::promise<BaseMessage::ptr>`，ASYNC 时用，结果通过它交给 future |
| `callback` | CALLBACK 时的用户回调 |
| `request` | 请求消息 |
| **`timer_id`** | muduo 定时器 ID，用于在收到响应时 **cancel** 掉超时定时器 |
| **`timeout_triggered`** | 是否已经走过一次超时逻辑，避免「响应晚到」和「超时」重复处理 |

### 1.2 两类「超时」来源

- **muduo 定时器**：`EventLoop::runAfter(timeout_sec, lambda)`，时间到在 **EventLoop 线程** 里执行 `onTimeout(req_id)`。
- **同步 send 的 wait_for**：用户线程 `async_resp.wait_for(timeout)` 超时后，**用户线程** 主动调一次 `onTimeout(req_id)`，并先 cancel 掉 muduo 定时器。

两者只会「生效」一次，靠 `timeout_triggered` 和「先到先处理、后到发现已处理就 return」保证。

### 1.3 线程模型

- **EventLoop 线程**：处理该连接上的收包、定时器回调。  
  - `onResponse` 一定在 EventLoop 里被调用（来自 TcpConnection 的消息回调 → Dispacher → `Requestor::onResponse`）。  
  - muduo 的 `runAfter` 回调也在同一 EventLoop 里执行，即 **定时器触发的 `onTimeout` 在 EventLoop 线程**。
- **用户线程**：调用 `send()` / `call()` 的线程。  
  - **同步 send** 里 `wait_for(timeout)` 超时后，是在 **用户线程** 里调 `onTimeout(req_id)`。

因此 `onTimeout` 可能来自两个线程，所以内部用 `_mutex` 和 `timeout_triggered` 做互斥与幂等。

---

## 二、发送请求时：如何挂上超时（只对 ASYNC）

### 2.1 入口

- **同步调用**：`send(conn, req, resp, timeout)`（例如 5s）。
- **异步调用**：内部也是 `send(conn, req, async_resp, timeout)`，只是外层用 `async_resp.get_future()` 再 `get()` / `wait_for()`。
- **回调调用**：`send(conn, req, cb)`，**不设定时器**，没有超时逻辑。

下面只讲「带超时的 send」，即两处 `send(..., timeout)` 共用的那套。

### 2.2 带超时的 send 调用链（ASYNC 分支）

```
send(conn, req, async_resp, timeout)   // 用户线程
  │
  ├─► newDesc(req, ReqType::ASYNC)
  │     └─ 持 _mutex，创建 ReqDescribe，写入 _request_desc[req->rid()]，返回 req_desc
  │
  ├─► muduo_conn = dynamic_cast<MuduoConnection*>(conn.get())
  │     getLoop() 得到该连接所属的 EventLoop
  │
  ├─► tid = getLoop()->runAfter(timeout_sec, [this, req_id](){ this->onTimeout(req_id); })
  │     │
  │     │  muduo 侧（简要）：
  │     │  • EventLoop::runAfter(delay, cb)
  │     │      → runAt(now+delay, cb)
  │     │      → timerQueue_->addTimer(cb, when, 0.0)
  │     │  • addTimer 里：new Timer(cb), runInLoop(addTimerInLoop, timer)，立即 return TimerId(timer, seq)
  │     │  • 所以 runAfter 是「把定时器加入队列」的异步动作，TimerId 先返回，真正插入在 loop 里做
  │     │  • 到期时：timerfd 可读 → TimerQueue::handleRead() → getExpired() → 对每个 Timer 调 it.second->run()
  │     │           即执行我们传入的 lambda → onTimeout(req_id)，且一定在 EventLoop 线程
  │     │
  │     └─ 返回 TimerId，用于之后 cancel
  │
  ├─► { lock(_mutex); req_desc->timer_id = tid; }
  │     为什么在锁里写：onResponse 在 EventLoop 里会读 req_desc->timer_id 并 cancel；
  │     若不在锁里写，可能 response 先到，读到未初始化的 timer_id，cancel 无效，定时器仍会再触发一次（逻辑仍对，但会多一次无效 onTimeout）
  │
  ├─► conn->send(req)                    // 真正发请求
  │
  ├─► async_resp = req_desc->response.get_future()
  │
  └─► return true
```

要点：

- 只有「带 timeout 的 send」才会 `runAfter`，所以 **CALLBACK 型 send 没有定时器**。
- `timer_id` 在 **持锁** 下写入，保证 `onResponse` 里能可靠地 cancel 到正确的定时器。
- 超时回调是「在 EventLoop 里过一段时间调 `onTimeout(req_id)`」，不依赖用户线程。

---

## 三、收到响应时：取消定时器并交付结果

### 3.1 谁在什么时候调 onResponse

网络层收到响应 → TcpConnection 回调（在 EventLoop）→ Dispacher 按消息类型分发 → 对 RPC 响应会调到：

`Requestor::onResponse(conn, msg)`，**始终在 EventLoop 线程**。

### 3.2 onResponse 内部流程

```
onResponse(conn, msg)                    // EventLoop 线程
  │
  ├─► id = msg->rid()
  ├─► req_desc = getDesc(id)             // 持 _mutex 查 _request_desc[id]
  │     若为 null：说明已超时并 delDesc 过了，或非法响应 → 打日志 return
  │
  ├─► {
  │      lock(_mutex);
  │      if (req_desc->timeout_triggered) {
  │          // 已超时处理过，忽略晚到的响应，只做清理
  │          delDescUnlocked(id);
  │          return;
  │      }
  │      // 未超时：取消定时器，防止之后定时器再触发 onTimeout
  │      muduo_conn->getLoop()->cancel(req_desc->timer_id);
  │        │
  │        │  muduo：timerQueue_->cancel(timerId) → runInLoop(cancelInLoop, timerId)
  │        │         若当前已在 loop 线程则直接 cancelInLoop，从 timers_/activeTimers_ 里删掉，到期就不会再 run
  │        │
  │  }
  │
  ├─► 按 reqtype 交付结果：
  │     ASYNC  → req_desc->response.set_value(msg)   // future 变为 ready
  │     CALLBACK → req_desc->callback(msg)
  │
  └─► delDesc(id)                         // 持锁 erase，释放 ReqDescribe
```

为什么先看 `timeout_triggered`：

- 若先发生了超时，`onTimeout` 里已经设了 `timeout_triggered = true` 并 `set_value(timeout_msg)`、`delDesc`。
- 此时 `getDesc(id)` 可能已经为 null（若已 delDesc），或在某些时序下仍能拿到尚未被删的 req_desc；若还能拿到且 `timeout_triggered` 已为 true，说明是「超时后晚到的响应」，应忽略并只做清理，不再 set_value 或 callback。

为什么一定要 cancel(timer_id)：

- 响应先于超时到达时，必须把已挂的定时器取消，否则到时还会执行一次 `onTimeout(req_id)`，造成重复处理。
- muduo 的 `TimerId` 没有 `operator!=`，不能写「if timer_id != 默认值再 cancel」，所以实现上直接对 `req_desc->timer_id` 调 `cancel()`；若从未设过定时器（如 CALLBACK 或没拿到 loop），timer_id 为默认值，muduo 里 cancel 是 no-op。

---

## 四、超时触发时：onTimeout 在两种路径下被谁调

### 4.1 路径 A：muduo 定时器到期（EventLoop 线程）

- 到期时：`TimerQueue::handleRead()` → `getExpired()` → 对过期项 `it.second->run()` → 我们的 lambda → **`onTimeout(req_id)`**，在 **EventLoop 线程**。

### 4.2 路径 B：同步 send 的 wait_for 超时（用户线程）

- 用户调的是 `send(conn, req, resp, timeout)`（同步版），内部：
  - 先调上面的「带超时 send」拿到 `async_resp`；
  - 然后 `async_resp.wait_for(timeout)` 在 **用户线程** 阻塞；
  - 若在 timeout 内没有 `set_value`，`wait_for` 返回 `future_status::timeout`，接着：
    - `getDesc(req->rid())` 拿到 desc（若还存在）；
    - `getLoop()->cancel(desc->timer_id)`，避免之后 EventLoop 里再触发一次 onTimeout；
    - 再 **用户线程** 调 **`onTimeout(req->rid())`**；
    - `return false`。

所以 **同一个请求**，要么是「定时器在 EventLoop 里调 onTimeout」，要么是「用户线程 wait_for 超时后调 onTimeout」，不会两边都「生效」一次（见下节）。

### 4.3 onTimeout 内部流程（无论谁调）

```
onTimeout(req_id)                        // 可能来自 EventLoop 或 用户线程
  │
  ├─► req_desc = getDesc(req_id)         // 持锁查找
  │     若为 null：说明已被 onResponse 或 上一次 onTimeout 清理掉 → 直接 return（幂等）
  │
  ├─► {
  │      lock(_mutex);
  │      if (req_desc->timeout_triggered) return;   // 已经处理过超时（防止重复）
  │      req_desc->timeout_triggered = true;
  │  }
  │
  ├─► 若是 ASYNC：
  │     timeout_msg = create RpcResponse(rcode=TIMEOUT)
  │     try { req_desc->response.set_value(timeout_msg); }
  │     catch { ... }   // 若 promise 已被 onResponse 设过，会抛，捕获后忽略
  │
  │     若是 CALLBACK：
  │     只打日志，不调用户 callback（当前设计是超时不通知回调）
  │
  └─► delDesc(req_id)                     // 持锁 erase，释放 ReqDescribe
```

要点：

- **timeout_triggered**：保证「超时逻辑」只执行一次；若先 onResponse 再 onTimeout（极短时间差），onTimeout 里 getDesc 可能仍非 null，但此时 promise 已 set，这里再 set_value 会抛，被 catch 掉即可。
- **delDesc**：超时后也删掉描述，和 onResponse 一样，避免重复使用和泄漏。

---

## 五、同步 send 的完整时间线（帮助理解「为什么」）

同步调用：

```cpp
send(conn, req, resp, timeout)  // 例如 5s
  → send(conn, req, async_resp, timeout)  // newDesc + runAfter(5) + conn->send + get_future
  → async_resp.wait_for(5s)                // 用户线程阻塞
```

可能情况：

1. **5 秒内收到响应**
   - EventLoop 调 `onResponse` → cancel(timer_id)、set_value(msg)、delDesc(id)。
   - 用户线程的 `wait_for` 被唤醒，返回 `ready`，`resp = async_resp.get()`，return true。之后定时器到期也不会再 run（已 cancel）。

2. **5 秒到了还没响应（wait_for 先超时）**
   - 用户线程从 `wait_for` 返回 `timeout` → getDesc、cancel(desc->timer_id)、`onTimeout(req_id)`、return false。
   - 若之后定时器本应触发，因为已经 cancel，EventLoop 里不会再调 onTimeout；若 cancel 尚未在 loop 里执行、定时器先触发，则 EventLoop 里会调 onTimeout，此时 getDesc 可能已为 null（用户线程 onTimeout 已 delDesc），直接 return，不会重复 set_value。

3. **几乎同时：定时器到期 + wait_for 超时**
   - 一个线程先进入 onTimeout，设 timeout_triggered、set_value(timeout_msg)、delDesc。
   - 另一个线程再进 onTimeout：要么 getDesc 为 null，要么 timeout_triggered 已为 true，直接 return。
   - 若 onResponse 和 onTimeout 几乎同时：一个先 set_value，另一个要么发现 timeout_triggered 已 true，要么在 set_value 时 catch 异常，都不会破坏 promise 单次 set 的语义。

所以：**为什么同步超时时要先 cancel 再 onTimeout**——避免 EventLoop 里稍后再执行一次 onTimeout，减少无谓调用和重复逻辑；即使用户线程已经 onTimeout 并 delDesc，cancel 仍能保证「定时器不会再跑」。

---

## 六、小结表

| 时机       | 谁调谁、在什么线程                         | 作用 |
|------------|--------------------------------------------|------|
| 发请求     | 用户线程：send → newDesc → runAfter → 锁内写 timer_id → conn->send | 登记请求、挂超时定时器、发数据 |
| 收响应     | EventLoop：onResponse → getDesc → 看 timeout_triggered → cancel(timer_id) → set_value/callback → delDesc | 取消超时、交付结果、清理 |
| 定时器到期 | EventLoop：lambda → onTimeout(req_id)       | 设 timeout_triggered、set_value(timeout_msg) 或仅日志、delDesc |
| 同步超时   | 用户线程：wait_for 超时 → cancel(timer_id) → onTimeout(req_id) → return false | 用户侧感知超时、避免定时器再触发 |

核心设计点：

- **只对 ASYNC 且带 timeout 的 send 设定时器**；CALLBACK 不设。
- **timer_id 在锁内写入**，保证 onResponse 能正确 cancel。
- **timeout_triggered + 先到先处理**，保证「响应」和「超时」只生效一次；promise 用 try/catch 防止二次 set_value。
- **同步超时路径先 cancel 再 onTimeout**，避免 EventLoop 再执行一次 onTimeout。
