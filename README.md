# SecureLicense-DUC Demo (C++17)

这个 Demo 用最小实现验证你的核心想法是否可落地：

- 云端下发授权 Token（`/activate`）
- 本地校验 Token 签名与过期时间
- 运行时云端心跳（`/heartbeat`）
- 网络中断时进入离线宽限期（grace period）
- 本地时间回退检测（防改系统时间的基础策略）

## 1. 构建

方式 A（推荐，当前环境可直接用）：

```bash
cd /data/zhenglingxin/projects/SecureLicense-DUC-demo
mkdir -p build
g++ -std=c++17 -O2 -Iinclude src/common.cpp src/http.cpp src/license_server_main.cpp -o build/license_server
g++ -std=c++17 -O2 -Iinclude src/common.cpp src/http.cpp src/license_client_main.cpp -o build/license_client
```

方式 B（如果你本机安装了 `cmake`）：

```bash
cd /data/zhenglingxin/projects/SecureLicense-DUC-demo
cmake -S . -B build
cmake --build build -j
```

生成：

- `build/license_server`
- `build/license_client`

也可以一键运行完整验证：

```bash
cd /data/zhenglingxin/projects/SecureLicense-DUC-demo
./run_demo.sh
```

## 2. 启动服务端

```bash
./build/license_server --port 8088 --duration 120
```

说明：`--duration 120` 表示授权有效期 120 秒（为了演示方便）。

## 3. 客户端激活并运行

激活：

```bash
./build/license_client activate --host 127.0.0.1 --port 8088 --machine my-lora-node --cache ./license_cache.txt
```

运行校验（在线）：

```bash
./build/license_client run --host 127.0.0.1 --port 8088 --machine my-lora-node --cache ./license_cache.txt --grace 20
```

## 4. 验证离线宽限

1) 保持已激活状态，先停掉服务端（Ctrl+C）
2) 立刻执行：

```bash
./build/license_client run --host 127.0.0.1 --port 8088 --machine my-lora-node --cache ./license_cache.txt --grace 20
```

预期：在 20 秒内仍然 `allowed (offline grace mode)`。

3) 等待超过 20 秒后再次执行同命令。

预期：`denied: heartbeat failed and grace exceeded`。

## 5. 设计说明

本 Demo 的目标是“验证架构思路可实现”，所以做了简化：

- 签名算法使用了轻量哈希签名（FNV1a）来减少依赖。
- 协议使用最小 HTTP（明文）便于阅读与调试。

在生产环境你应替换为：

- TLS + 双向证书认证
- HMAC-SHA256 / Ed25519 / ECDSA 等标准签名
- 安全密钥管理（KMS、密钥轮换）
- 更强的本地防篡改与审计日志
