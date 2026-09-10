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

> 状态：交接记录与实现边界审计。本文只记录当前仓库可验证的事实和交接验收点，不否定任何已完成工作，但区分“部分层已完成”和“生成程序 Bridge Endpoint 已完成”。

## 结论

当前 PC 生成程序的控制/观测链路可以运行，但它使用的是 `host_win.c` 的文本行协议，不是标准 Bridge 二进制链路。换句话说，现有能力可以支撑当前 UI 的实时运行与 Probe 展示，但还不能证明“配置好即完整程序”已内建标准 Bridge Endpoint。

应按层拆分理解：

| 层 | 当前状态 | 说明 |
|---|---|---|
| Bridge 协议语义 | 已实现 | Python `BridgeSession` 已有 CALL/RESPONSE/NOTIFICATION、超时、重试、Probe 和 id_map 语义。 |
| HLOS Transport | 已实现 | stdio/process、TCP、Windows Named Pipe/POSIX Unix Socket 已存在，可作为字节 Transport。 |
| UART Framing | 已实现 | OLINK Framing 用于串口侧成帧，属于可组合 Codec/Transport 层。 |
| 生成图 Backend 材料 | 已实现 | 生成代码包含控制槽、id_map、Probe 槽和 `orpheus_control_message()`。 |
| 生成 exe 文本宿主 | 已实现 | `host_win.c` 支持 stdin 文本命令、stdout 文本日志/Probe。 |
| 生成 exe 标准 Bridge Endpoint | 未闭环 | 还没有在生成 exe 内完成 Bridge 帧解码、CALL/RESPONSE 匹配、能力协商、订阅和 Transport 状态管理。 |
| 独立程序生命周期 | 待验证/待强化 | 工程切换、异常退出、服务关闭后的残留进程治理仍需按方案验收。 |

## 为什么当前链路能反馈运行数据

当前实时反馈不是通过标准 Bridge Endpoint，而是通过文本实时会话：

```text
音频线程
  -> 组件 Probe 槽
  -> host_win.c 定时读取
  -> printf 输出 PROBE/PROBE_JSON 文本行
  -> stdout
  -> RtSession 解析文本行
  -> FastAPI/UI
```

参数下发链路也类似：

```text
UI
  -> FastAPI/RtSession
  -> stdin 写 SET/GET/BULK/MSG 文本命令
  -> host_win.c 解析命令
  -> orpheus_control_message()/控制槽
  -> 生成图
```

其中 `MSG <hex>` 会调用生成代码的 `orpheus_control_message()`，并把响应封装回 `MSGRSP <hex>`。这说明二进制消息 Backend 已存在，但外层仍是文本宿主协议。

## 已完成的材料

以下代码是可复用基础：

- `orpheus_core/orpheus_core/bridge/core.py`：主机侧 BridgeSession 与协议语义。
- `orpheus_core/orpheus_core/bridge/hlos_transport.py`：stdio/process、TCP、Pipe 等 HLOS Transport。
- `orpheus_core/orpheus_core/generator.py`：生成静态图、控制槽、id_map、Probe 槽和 `orpheus_control_message()`。
- `orpheus_core/orpheus_core/templates/host_win.c`：当前 PC 实时宿主、设备时钟、文本控制协议和 Probe 上报。
- `orpheus_core/orpheus_core/server/rt.py`：当前文本实时会话管理、stdout Probe 解析和子进程生命周期基础。

## 关键差异

`host_win.c` 的 `MSG <hex>` 不等于 Bridge Endpoint：

1. 它依赖宿主外层把二进制消息转换为十六进制文本。
2. Probe 是宿主定时打印的文本行，不是 Bridge NOTIFICATION。
3. 没有标准 HELLO/IDENTITY、能力协商、订阅、流控和 Transport 状态机。
4. `BridgeSession + StdioTransport + LengthPrefixCodec` 不能直接连接当前 `host_win.c`。
5. 它没有建立“生成 exe 就是 Bridge 服务端”的完整契约。

`async_bridge` / `rate_bridge` 是音频 Task 间的数据桥；Access Bridge 是控制/观测访问桥。两者不能混用。

## 交接澄清问题

如果原开发者声称 Bridge 已完成，需要补充确认以下事实：

1. 完成的是哪一层：BridgeSession、Transport、Backend、UART 链路，还是生成 exe Endpoint？
2. 是否存在另一个分支或未合入的 C 端 Bridge Endpoint 实现？
3. 生成 exe 是否能直接接收 Bridge 二进制帧，而不是 `MSG <hex>` 文本包装？
4. Probe 是通过 Bridge NOTIFICATION、轮询 RESPONSE，还是仍然通过 stdout 文本行？
5. HELLO/IDENTITY、protocol version、graph hash、plan hash、id_map hash 是否已经返回？
6. 有哪些测试用 Python `BridgeSession` 直接连到生成 exe？
7. STOP 后是否等待进程真实退出？工程切换和服务退出是否清理子进程？
8. 生成程序脱离 Python/Node/源码目录后，调参和监控是否仍然可用？

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

## 交接建议

1. 原实现者提供分支、提交、测试名和运行命令。
2. 若只有主机侧 BridgeSession 或 UART 链路完成，应在 PR/任务中改写为“Bridge 主机侧已完成，生成 exe Endpoint 未合入”。
3. 若确有生成 exe Endpoint 实现，请补一个从生成 exe 到 `BridgeSession` 的最小集成测试。
4. 后续实施范围以 [[pc-generated-app|PC 配置好即完整程序]] 为准。
