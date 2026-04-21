# CICD 补全指南（面向本仓库）

先把概念对齐：**CI（持续集成）** = 每次提交自动「能编、能测」；**CD（持续交付/部署）** = 把可运行产物交给环境或用户。个人/开源项目做到 **CI + 可选发版** 通常就够叫「完整 CICD 入门」；**自动部署到服务器** 只有在你真有线上环境时才需要。

---

## 一、你现在已具备什么（勾选）

| 层级 | 内容 | 状态 |
|------|------|------|
| 单元测试 | `lcz_rpc_unit_tests`（GTest） | 已有 |
| 构建 | CMake，全量 examples + tests | 已有 |
| 流水线文件 | `.github/workflows/ci.yml` | 已有 |
| 子模块 | CI 里 `submodules: recursive` | 已有 |

---

## 二、距离「完整」还常见缺什么（按优先级）

### P0 — 仓库策略（不算代码，但算 CICD 闭环）

**缺什么：** PR 合并不看 CI 也能点合并，门禁形同虚设。

**怎么补：**

1. GitHub 打开：**Settings → Branches → Branch protection rules → Add rule**
2. Branch name pattern 填：`main`（或你的主分支名）
3. 勾选：
   - **Require a pull request before merging**（按需）
   - **Require status checks to pass before merging**
   - 在搜索框里选中 **`CI`**（即 `ci.yml` 里的 job 名对应的 check）

保存后：**只有 CI 绿才能合并**，这才是「完整 CI 门禁」。

---

### P1 — CI 本身可再加强（可选）

| 缺什么 | 怎么补 |
|--------|--------|
| 同一分支连推多次，旧任务仍跑完浪费分钟 | 在 `ci.yml` 里加 `concurrency`（本仓库已可加） |
| Fork PR 的权限最小化 | `permissions: contents: read`（本仓库已可加） |
| 编译太慢 | 用 `ccache` + `actions/cache`（进阶，可后做） |

---

### P2 — 「CD」里最常见的：发版（Release）

**缺什么：** 只有源码，没有「打 tag 自动生成 Release + 附件」。

**怎么补（思路）：**

1. 本地打标签：`git tag v0.1.0 && git push origin v0.1.0`
2. 新建 `.github/workflows/release.yml`：`on: push: tags: ['v*']`，步骤与 CI 类似：checkout、装依赖、cmake、全量编译，再用 **`actions/upload-artifact`** 或 **`softprops/action-gh-release`** 上传 `rpc/build/bin` 打成的 zip（注意体积与许可证）。

若你不需要给用户预编译包，**可只做「Release 页面 + 源码 zip」**，GitHub 默认会附带源码，工作量最小。

---

### P3 — 部署（真 CD）

**缺什么：** 自动 SSH、Docker、K8s。

**怎么补：** 仅当你有**固定环境**时再做：

- 在 GitHub **Settings → Secrets** 存 `SSH_PRIVATE_KEY`、`HOST` 等；
- 另建 `deploy.yml`：`on: workflow_dispatch` 或 `push: branches: [main]`，用 `appleboy/ssh-action` 等执行脚本。

**没有线上机器就不必做**，不算「入门 CICD」的必选项。

---

### P4 — 测试再补一层（质量，非流水线文件）

| 缺什么 | 怎么补 |
|--------|--------|
| 单测不覆盖 TCP/注册中心 | 加脚本：后台起 `registry_server` + `rpc_server`，再跑 client，用固定端口或端口探测（集成测试） |
| 只信本地 | CI 里在「Run unit tests」后加一步跑该脚本（注意 `&` 后台与 `sleep`） |

---

## 三、建议的「补全顺序」（照着做即可）

1. **开分支保护 + 必选 CI**（P0）—— 5 分钟，收益最大  
2. **确认 CI 在 GitHub 上绿**（推送一次或开 PR 试）  
3. 需要对外发二进制再 **加 release workflow**（P2）  
4. 有服务器再 **加 deploy + Secrets**（P3）  
5. 有精力再 **集成测试 + 缓存编译**（P4 / P1）

---

## 四、本仓库路径备忘

- 仓库根：`RPC/`（含 `README.md`、`rpc/`、`.github/`）
- CMake 根：`rpc/`
- 可执行文件（examples）：`rpc/build/bin/`
- 单元测试：`rpc/build/tests/lcz_rpc_unit_tests`

---

## 五、常见问题

**Q：为什么本地能过、CI 失败？**  
A：缺依赖、子模块未初始化、或 GCC/CMake 版本与 `ubuntu-22.04` 不一致。以 CI 日志为准，在 `ci.yml` 的 `apt install` 里补包或固定版本。

**Q：`ctest` 在 CI 里找不到测试？**  
A：可继续像现在一样**直接运行** `./tests/lcz_rpc_unit_tests`；与 `gtest_discover_tests` 是否注册成功无关。

---

文档版本：与当前 `.github/workflows/ci.yml` 行为一致时可维护本节；流水线变更时请同步更新「五」中路径说明。
