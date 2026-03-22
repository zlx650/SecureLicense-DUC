# SecureLicense-DUC

SecureLicense-DUC 是一个基于 C++17 的分布式授权控制样例项目，面向“云端时间授权 + 本地容错运行”场景。项目实现了授权签发、心跳校验、离线宽限与基础防时间回退机制，可作为后续工程化版本的基础。

## 功能特性

- 云端授权签发：服务端通过 `/activate` 为指定机器签发授权 Token。
- 在线运行校验：客户端通过 `/heartbeat` 与服务端进行周期性校验。
- 离线宽限策略：服务不可达时，客户端在宽限时间内可继续运行。
- 时间完整性保护：客户端记录本地与服务端时间，检测明显时间回退。
- 标准签名：基于 HMAC-SHA256 生成和校验 Token 签名。
- 结构化日志：服务端与客户端均输出包含 `level` 和 `request_id` 的 JSON 日志。
- SQLite 持久化：授权状态落盘，服务重启后可恢复授权策略。
- 配置文件化：支持 `config/*.conf`，并可通过命令行参数覆盖。
- 一键演示脚本：`run_demo.sh` 可完整复现授权全流程。

## 项目结构

```text
SecureLicense-DUC-demo/
├── .github/workflows/
│   └── ci.yml
├── config/
│   ├── client.conf
│   └── server.conf
├── docs/
│   ├── CHANGELOG.md
│   ├── OPTIMIZATION_ROADMAP.md
│   └── TEST_RECORD.md
├── include/
│   ├── config.hpp
│   ├── common.hpp
│   ├── http.hpp
│   ├── log.hpp
│   └── storage.hpp
├── scripts/
│   └── test_regression.sh
├── tools/benchmark/
│   └── bench_license_flow.sh
├── src/
│   ├── config.cpp
│   ├── common.cpp
│   ├── http.cpp
│   ├── log.cpp
│   ├── storage.cpp
│   ├── license_server_main.cpp
│   └── license_client_main.cpp
├── tests/
│   └── test_duccore.cpp
├── run_demo.sh
├── CMakeLists.txt
└── README.md
```

## 构建

### 方式 A：使用 g++（推荐）

```bash
cd /data/zhenglingxin/projects/SecureLicense-DUC-demo
# 若 SQLite 安装在 conda 环境，请先激活该环境
conda activate /data/zhenglingxin/.conda/envs/cproject
mkdir -p build
g++ -std=c++17 -O2 -Iinclude -I"$CONDA_PREFIX/include" src/config.cpp src/common.cpp src/http.cpp src/log.cpp src/storage.cpp src/license_server_main.cpp -L"$CONDA_PREFIX/lib" -lsqlite3 -o build/license_server
g++ -std=c++17 -O2 -Iinclude src/config.cpp src/common.cpp src/http.cpp src/log.cpp src/license_client_main.cpp -o build/license_client
g++ -std=c++17 -O2 -Iinclude -I"$CONDA_PREFIX/include" src/config.cpp src/common.cpp src/http.cpp src/log.cpp src/storage.cpp tests/test_duccore.cpp -L"$CONDA_PREFIX/lib" -lsqlite3 -o build/duccore_tests
```

### 方式 B：使用 CMake

```bash
cd /data/zhenglingxin/projects/SecureLicense-DUC-demo
cmake -S . -B build
cmake --build build -j
```

## 快速开始

### 1) 启动授权服务端

```bash
./build/license_server --port 8088 --duration 120
```

参数说明：

- `--port`：服务端监听端口。
- `--duration`：授权有效期（秒）。
- `--db`：SQLite 数据库文件路径（默认 `./license_store.db`）。
- `--config`：服务端配置文件路径（默认 `./config/server.conf`，存在则自动加载）。
- `--log-level`：日志等级（`DEBUG/INFO/WARN/ERROR`）。

仅使用配置文件启动示例：

```bash
./build/license_server --config ./config/server.conf
```

### 2) 客户端激活授权

```bash
./build/license_client activate --host 127.0.0.1 --port 8088 --machine my-lora-node --cache ./license_cache.txt
```

### 3) 客户端运行校验

```bash
./build/license_client run --host 127.0.0.1 --port 8088 --machine my-lora-node --cache ./license_cache.txt --grace 20
```

参数说明：

- `--machine`：机器标识。
- `--cache`：本地授权缓存路径。
- `--grace`：离线宽限时长（秒）。
- `--config`：客户端配置文件路径（默认 `./config/client.conf`，存在则自动加载）。
- `--log-level`：日志等级（`DEBUG/INFO/WARN/ERROR`）。

仅使用配置文件运行示例：

```bash
./build/license_client activate --config ./config/client.conf
./build/license_client run --config ./config/client.conf
```

命令行覆盖配置文件示例：

```bash
./build/license_client activate --config ./config/client.conf --port 18091 --machine qa-node
```

## 一键流程验证

```bash
cd /data/zhenglingxin/projects/SecureLicense-DUC-demo
./run_demo.sh
```

脚本会自动完成以下流程：

1. 激活授权。
2. 在线运行校验。
3. 停止服务端，验证宽限期内可运行。
4. 超过宽限期后再次运行，验证拒绝策略。

## 回归测试

```bash
cd /data/zhenglingxin/projects/SecureLicense-DUC-demo
./scripts/test_regression.sh
```

该脚本覆盖以下核心场景：

1. 在线激活成功。
2. 在线心跳放行。
3. 离线宽限期内放行。
4. 离线超时拒绝。
5. 授权过期拒绝。
6. Token 篡改拒绝。
7. 机器标识不匹配拒绝。
8. 错误密钥拒绝。
9. 服务重启后授权状态保留（SQLite 持久化验证）。
10. 仅依赖配置文件完成激活/运行。
11. 验证命令行参数可覆盖配置文件参数。
12. 压测脚本冒烟运行（并发激活 + 并发心跳）。

测试结果输出到 `test_artifacts/latest_test_report.txt`。

也可以单独运行单元测试：

```bash
./build/duccore_tests
```

## 压测脚本

用于快速评估吞吐与稳定性（非严格性能基准）：

```bash
cd /data/zhenglingxin/projects/SecureLicense-DUC-demo
./tools/benchmark/bench_license_flow.sh --parallel 16 --activate-requests 100 --run-requests 100
```

输出示例字段：

- `activate total/success/fail/qps`
- `run total/success/fail/qps`
- `result: PASS/FAIL`

如果你已经提前构建过二进制，可加 `--skip-build` 缩短执行时间。

## CI 自动化

项目已提供 GitHub Actions 工作流：`.github/workflows/ci.yml`，触发条件：

- push 到 `main`
- 对 `main` 发起 pull request

CI 执行内容：

1. 安装 `g++` 与 `sqlite3` 依赖。
2. 运行 `./scripts/test_regression.sh`。

## HTTP 接口

### GET `/activate`

请求示例：

```text
/activate?machine=my-lora-node
```

成功响应字段：

- `status`
- `machine`
- `token`
- `server_time`
- `expires_at`

### GET `/heartbeat`

请求示例：

```text
/heartbeat?machine=my-lora-node&token=xxx
```

成功响应字段：

- `status`
- `server_time`
- `expires_at`

## 当前实现说明

当前版本为 MVP 样例实现，重点在流程验证。为方便本地调试，采用了轻量实现方式。用于生产环境时，建议补齐以下能力：

- TLS（含双向证书认证）
- 更强签名方案（如 Ed25519 / ECDSA）与密钥隔离
- KMS 密钥托管与密钥轮换
- 完整审计日志与异常告警
- 更强的客户端完整性保护

## 工程记录规范

项目采用以下固定记录文件，建议每次大更新后同步维护：

- 版本记录：`docs/CHANGELOG.md`
- 测试记录：`docs/TEST_RECORD.md`
- 优化规划：`docs/OPTIMIZATION_ROADMAP.md`

## 许可证

可按需求使用 MIT 或 Apache-2.0。
