# LCZ RPC 多终端演示手册

这份文档只讲演示操作，面试时建议按这里开 3~4 个终端手动跑，效果比脚本直观。

---

## 1. 演示前准备

```bash
cd /home/lcz/rpc/RPC/rpc
mkdir -p build && cd build
cmake ..
make -j
```

可执行文件目录：

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
```

---

## 2. 演示 A：JSON vs Protobuf 性能对比

目标：展示序列化方式切换后 QPS/延迟差异。

### 终端 A（JSON 服务端）

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
./benchmark_server_json 8889 0 0
```

### 终端 B（JSON 压测）

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
./benchmark_client_json single add 20000 0 0 0 127.0.0.1 8889 0
./benchmark_client_json multi add 20000 4 0 0 0 127.0.0.1 8889 0
./benchmark_client_json throughput add 0 0 10 0 127.0.0.1 8889 0
./benchmark_client_json single echo 1000 0 0 0 127.0.0.1 8889 0 100000
```

### 切到 Protobuf

终端 A 先 `Ctrl+C` 停掉 JSON 服务端，再运行：

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
./benchmark_server 8889 0 0
```

终端 B 运行：

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
./benchmark_client single add 20000 0 0 0 127.0.0.1 8889 0
./benchmark_client multi add 20000 4 0 0 127.0.0.1 8889 0
./benchmark_client throughput add 0 0 10 0 127.0.0.1 8889 0
./benchmark_client single echo 1000 0 0 0 127.0.0.1 8889 0 100000
```

口述建议：
- 大包（100KB echo）下 P99、QPS 差距更明显。
- 小包（add）也有收益，但受机器负载影响更大。

---

## 3. 演示 B：注册中心 + 服务发现 + 下线处理

目标：展示“注册成功 -> 调用成功 -> 服务下线 -> 客户端再调用失败”。

### 终端 A（Registry）

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
./test4_registry_server
```

### 终端 B（Provider）

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
./test4_provider_server
```

关键成功信号（Provider 终端）：
- `[Provider] 注册成功 method=add host=127.0.0.1:8889 load=10`

### 终端 C（Consumer，首次调用）

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
./test4_consumer_client
```

关键成功信号（Consumer 终端）：
- `调用成功，结果: 30`

### 模拟服务下线 + 二次验证

在终端 B 按 `Ctrl+C` 关闭 Provider。

再开终端 D（或复用终端 C）再次执行：

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
./test4_consumer_client
```

关键失败信号（Consumer 终端）：
- `服务发现失败，没有提供 add 服务的主机`

Registry 可见信号：
- `[Registry] 连接断开，触发下线处理`
- 若走超时剔除，还会看到过期剔除打印。

---

## 4. 演示 C：test1 轻量版（三终端）

### 终端 A

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
./test1_registry_server
```

### 终端 B

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
./test1_rpc_server
```

### 终端 C

```bash
cd /home/lcz/rpc/RPC/rpc/build/bin
./test1_rpc_client
```

---

## 5. 打开日志限制（默认只打 ELOG）

当前日志宏在 `rpc/src/general/detail.hpp`，默认：

```cpp
if(level>=LERR)/*只打印错误日志*/
```

如果要看到 `ILOG/WLOG/DLOG`，改成：

```cpp
if(level>=LDEFAULT)
```

然后重新编译：

```bash
cd /home/lcz/rpc/RPC/rpc/build
make -j
```

> 注意：日志会显著变多，演示时建议只开必要终端窗口。

