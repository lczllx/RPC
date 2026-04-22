# Docker 使用说明（LCZ RPC）

## 还剩哪些（CICD 里 Docker 之外）

| 项 | 状态 |
|----|------|
| CI 编译 + 单测 | 已有 |
| Release（tag → zip） | 已有 |
| **Dockerfile + CI 里 `docker build` 校验** | 已有 |
| 推镜像到 GHCR / Docker Hub | **未做**（需账号与 `docker login`，见文末） |
| 一键 `docker-compose` 起 registry+server+client | **未做**（可按需加 `compose.yml`） |
| K8s / 自动部署 | **未做**（需集群与 Secrets） |

---

## 本地怎么构建镜像

在**仓库根** `RPC/`（与 `Dockerfile` 同级），且 **muduo 子模块已拉取**：

```bash
git submodule update --init --recursive
docker build -t lcz-rpc:local .
```

成功则得到镜像 `lcz-rpc:local`。

### 拉取 `ubuntu:22.04` 超时（`registry-1.docker.io` i/o timeout）

这是**访问 Docker Hub 不畅**（网络、防火墙、DNS），不是 Dockerfile 写错。

**云服务器上只想把镜像拉下来：配「镜像加速」即可，不必给 Docker 配 HTTP 代理**（代理在云机、本机混用还容易配错）。

1. **配置镜像加速（推荐）**  
   编辑 `/etc/docker/daemon.json`（没有则新建），例如：

   ```json
   {
     "registry-mirrors": [
       "https://你的镜像地址"
     ]
   }
   ```

   镜像地址在所用云厂商控制台申请（如阿里云「容器镜像服务」→ 镜像加速器，会给出专属 URL）。保存后执行：

   ```bash
   sudo systemctl daemon-reload
   sudo systemctl restart docker
   ```

   再试：`docker pull ubuntu:22.04`，能通后再 `docker build`。

2. **（可选）给 Docker daemon 配 HTTP/HTTPS 代理**  
   多数场景用 **[1] 镜像加速**就够；若你**坚持用笔记本上的 Clash**，可以按下面做。`docker pull` / `docker build` 拉基础镜像走 **Docker 守护进程**，要在 **daemon** 上配环境变量（不是只给当前 shell）。

   **A. 代理与 Docker 在同一台机器（云服务器本机装了 Clash）**  

   HTTP 端口与 Clash 一致，示例 **`http://127.0.0.1:7899`**。在云服务器执行：

   ```bash
   sudo mkdir -p /etc/systemd/system/docker.service.d
   sudo tee /etc/systemd/system/docker.service.d/http-proxy.conf <<'EOF'
   [Service]
   Environment="HTTP_PROXY=http://127.0.0.1:7899"
   Environment="HTTPS_PROXY=http://127.0.0.1:7899"
   Environment="NO_PROXY=localhost,127.0.0.1,.local"
   EOF
   sudo systemctl daemon-reload
   sudo systemctl restart docker
   ```

   验证：`sudo systemctl show --property=Environment docker`。

   **B. 代理在笔记本、Docker 在云服务器（SSH 反向端口转发）**  

   **`ssh -R` 必须由笔记本发起**：在笔记本上打开终端（Clash 已开，HTTP 端口以下用 `7899`），**不要**在云服务器上 `ssh` 连自己——那样隧道到的是云机本机，到不了笔记本。

   在**笔记本**执行（`用户`、`云服务器公网 IP` 换成你的；**冒号后是笔记本 Clash 的 HTTP 端口**，常见 `7899`）：

   ```bash
   ssh -o ServerAliveInterval=30 -R 17899:127.0.0.1:7899 用户@云服务器公网IP
   ```

   含义：**云服务器**监听 `127.0.0.1:17899`，经 SSH 转到**笔记本** `127.0.0.1:7899`（Clash）。远端用 `17899` 是为了避开云机上常占用的 `7899`（见下）。

   **保持这条 SSH 不断开**（可另开窗口干活）。Clash 一般只需监听笔记本 `127.0.0.1`，**不必**为此开「允许局域网」。

   然后在**云服务器**上把 Docker 代理指到隧道端口（与 **A** 相同做法，但地址用隧道端口）：

   ```text
   HTTP_PROXY=http://127.0.0.1:17899
   HTTPS_PROXY=http://127.0.0.1:17899
   ```

   保存 `http-proxy.conf` 后 `daemon-reload` + `restart docker`。

   **若出现 `Warning: remote port forwarding failed for listen port …`：** 表示云服务器**监听不了**你写的那个远端端口（常见原因：该端口已被占用，如云上也装了 Clash 并占 `7899`）。在云服务器执行 `ss -lntp | grep 7899`（或 `17899`）可看占用；**换一个远端未占用端口**即可，例如把 `-R` 改成 `-R 27999:127.0.0.1:7899`，并把 `http-proxy.conf` 改成 `http://127.0.0.1:27999`。

   若你确认云上 **`7899` 空闲**，也可把示例改回 `-R 7899:127.0.0.1:7899`，Docker 里对应 `http://127.0.0.1:7899`。

   若代理是 **SOCKS5**，优先用 Clash 提供的 **HTTP 端口** 给 Docker 用。

   **说明：** `~/.docker/config.json` **不能替代** daemon 的拉镜像代理。Dockerfile 里 `RUN apt-get` 若也要走代理，需 `docker build` 时传 `--build-arg HTTP_PROXY=...`（本仓库 Dockerfile 若加 `ARG` 再配）。

3. **换网络 / VPN 后再 `docker pull ubuntu:22.04` 试一次**

4. **GitHub Actions**  
   CI 里的 `docker build` 跑在 GitHub 境外 runner 上，**一般不受你本机访问 Docker Hub 的影响**；本机失败不代表 CI 失败。

---

## 运行容器

默认进入镜像会打印 `/opt/rpc/bin` 列表：

```bash
docker run --rm -it lcz-rpc:local
```

跑某个示例（端口按程序默认，自行 `-p` 映射）：

```bash
docker run --rm -p 7070:7070 lcz-rpc:local /opt/rpc/bin/test1_registry_server
```

多容器编排（registry + provider + consumer）建议自己写 **`docker-compose.yml`**，或继续用本机多终端。

---

## 镜像里为什么没单测

`Dockerfile` 里 `LCZ_RPC_BUILD_TESTS=OFF`，减小体积、且运行时不需要 GTest。需要镜像内跑测试可改为 `ON` 并安装 `libgtest-dev`（镜像会变大）。

---

## CI 里做了什么

`.github/workflows/ci.yml` 中的 **`docker-image` job** 在每次 PR/push 时执行 `docker build`，**不推送**到任何镜像仓库，只保证 **Dockerfile 不烂**。

---

## （可选）推送到 GitHub Container Registry (GHCR)

需在本仓库 **Settings → Actions → General** 勾选 workflow 写权限，并在 workflow 里增加：

- `permissions: packages: write`
- `docker/login-action` + `docker/build-push-action`，镜像名如 `ghcr.io/<owner>/rpc:tag`

此为进阶，与「能本地/CI 构建镜像」独立。
