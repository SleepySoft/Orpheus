---
title: PC 生成程序 Bridge 交接审计
type: HOW
up: '[[00-index]]'
related:
  - '[[pc-generated-app]]'
  - '[[bridge-protocol]]'
  - '[[hlos-transport]]'
tags: [orpheus/how, codegen, bridge, handover]
---

# PC 生成程序 Bridge 交接审计

> 状态：审计已闭环。本文记录统一 Endpoint 的当前可验证事实与回归矩阵。

## 结论

PC 生成程序已内建标准二进制 Bridge Endpoint。动态 Runtime、生成 PC 程序和 DSP/UART 使用同一 §18 数据路由、系统握手、编译器身份与 BridgeSession API。

| 层 | 当前状态 | 说明 |
|---|---|---|
| Bridge 协议语义 | 已实现 | Python `BridgeSession` 已有 CALL/RESPONSE/NOTIFICATION、超时、重试、Probe 和 id_map 语义。 |
| HLOS Transport | 已实现 | stdio/process、TCP、Windows Named Pipe/POSIX Unix Socket 已存在，可作为字节 Transport。 |
| UART Framing | 已实现 | OLINK Framing 用于串口侧成帧，属于可组合 Codec/Transport 层。 |
| 生成图 Backend 材料 | 已实现 | 生成代码包含控制槽、id_map、Probe 槽和 `orpheus_control_message()`。 |
| C Bridge Endpoint | 已实现 | HELLO、IDENTITY、STOP、STATS、分页 MAP 与 Backend dispatch。 |
| 生成 exe 标准 Bridge Endpoint | 已实现 | `host_win` 和 `host_cli` 使用 LengthPrefix 二进制 stdio；日志仅写 stderr/file sink。 |
| 独立程序生命周期 | 已实现 | 后端拥有子进程，STOP 等待退出并在超时后升级终止；工程删除、切换和服务关闭均清理会话。 |

## 当前链路

```text
音频线程
  -> 组件 Probe 槽
  -> GeneratedBackend / RuntimeBackend
  -> Bridge Endpoint RESPONSE
  -> LengthPrefix 或 OLINK
  -> BridgeSession 轮询
  -> FastAPI/UI
```

参数下发链路也类似：

```text
UI
  -> FastAPI/BridgeSession
  -> §18 CALL
  -> Bridge Endpoint
  -> orpheus_control_message()/Runtime::message()
  -> 生成图
```

## 已完成的材料

以下代码是可复用基础：

- `orpheus_core/orpheus_core/bridge/core.py`：主机侧 BridgeSession 与协议语义。
- `orpheus_core/orpheus_core/bridge/hlos_transport.py`：stdio/process、TCP、Pipe 等 HLOS Transport。
- `orpheus_core/orpheus_core/generator.py`：生成静态图、控制槽、id_map、Probe 槽和 `orpheus_control_message()`。
- `orpheus_core/orpheus_core/templates/host_win.c`：PC 实时宿主、设备时钟和 Bridge stdio。
- `orpheus_core/orpheus_core/server/bridge_process_session.py`：握手、日志、Probe 轮询与子进程生命周期。

`async_bridge` / `rate_bridge` 是音频 Task 间的数据桥；Access Bridge 是控制/观测访问桥。两者保持独立。

## 最小验收矩阵

交接实现必须给出可复现证据：

| 用例 | 通过标准 |
|---|---|
| stdio Bridge loopback | Python `BridgeSession` 通过 stdio 连接生成 exe，CALL 能得到匹配 RESPONSE。 |
| 参数写入 | 通过 Bridge SET 修改参数，生成图运行行为真实变化。 |
| Probe 读取 | 通过 Bridge 轮询或 NOTIFICATION 获得 Probe，不依赖文本 `PROBE` 行。 |
| 身份校验 | HELLO 返回 protocol/hash/能力；hash 不一致时拒绝写操作。 |
| STOP | 发送 Bridge STOP 后，transport 关闭，进程真实退出。 |
| 工程切换 | 切换工程后旧生成 exe 不残留。 |
| 独立运行 | 复制生成目录到无源码环境，程序仍能运行并通过 Bridge 访问。 |
| 一致性 | 同一工程动态运行与生成程序运行的参数、控制链路和 Probe 行为一致。 |

## 后续边界

Core Profile 已闭环；后续工作是主动 NOTIFICATION/订阅、幂等响应缓存、BULK 分片、TLS/lease、多客户端和 SHM/RPMsg 多 Lane，不再保留文本协议兼容层。
