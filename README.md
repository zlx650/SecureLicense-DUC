# SecureLicense-DUC

SecureLicense-DUC 是一个基于 C++17 的分布式授权控制样例项目，面向“云端时间授权 + 本地容错运行”场景。项目实现了授权签发、心跳校验、离线宽限与基础防时间回退机制，可作为后续工程化版本的基础。

## 功能特性

- 云端授权签发：服务端通过 `/activate` 为指定机器签发授权 Token。
- 在线运行校验：客户端通过 `/heartbeat` 与服务端进行周期性校验。
- 离线宽限策略：服务不可达时，客户端在宽限时间内可继续运行。
- 时间完整性保护：客户端记录本地与服务端时间，检测明显时间回退。
- 一键演示脚本：`run_demo.sh` 可完整复现授权全流程。

## 项目结构

```text
SecureLicense-DUC-demo/
├── include/
│   ├── common.hpp
│   └── http.hpp
├── src/
│   ├── common.cpp
│   ├── http.cpp
│   ├── license_server_main.cpp
│   └── license_client_main.cpp
├── run_demo.sh
├── CMakeLists.txt
└── README.md
```

## 构建

### 方式 A：使用 g++（推荐）

```bash
cd /data/zhenglingxin/projects/SecureLicense-DUC-demo
mkdir -p build
g++ -std=c++17 -O2 -Iinclude src/common.cpp src/http.cpp src/license_server_main.cpp -o build/license_server
g++ -std=c++17 -O2 -Iinclude src/common.cpp src/http.cpp src/license_client_main.cpp -o build/license_client
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
- 标准签名算法（如 HMAC-SHA256 / Ed25519 / ECDSA）
- KMS 密钥托管与密钥轮换
- 完整审计日志与异常告警
- 更强的客户端完整性保护

## 许可证

可按需求使用 MIT 或 Apache-2.0。
