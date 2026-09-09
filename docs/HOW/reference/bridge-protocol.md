# Bridge 交互协议与能力分层

> 状态：架构定案（v1）+ P0 主机核心已实现。本文定义 Access Bridge 的交互基线、双工能力、Transport 边界、HLOS 扩展和日志策略；整体分层见 `design_access_bridge.md`。

## 1. 决策

完整 Access Bridge 的最低能力是**双向半双工**，不是单工：

- 任一时刻最多一个未完成 CALL；
- Endpoint 完成处理后返回匹配 call_id 的 RESPONSE；
- RESPONSE 优先于观测数据；
- Observation 由主机轮询或在明确授予的发送窗口内返回；
- 超时可使用同一 call_id 重试，Endpoint 应支持幂等响应缓存。

全双工是能力升级：允许主动 NOTIFICATION、多个 outstanding CALL 和乱序 RESPONSE，但不改变消息信封、route/data ID、错误语义或 Backend。

真正单工仅能形成 `tx_only` 遥测或 `rx_only` 无确认写入，不属于完整 Bridge Profile，UI 不得将其呈现为在线调音会话。

## 2. 分层与替换边界

```text
UI / REST / SDK
       |
BridgeSession：CALL 匹配、超时重试、能力、订阅、协议级流控
       |
FrameCodec：OLINK / 长度前缀 / datagram
       |
ByteTransport：UART / Pipe / TCP / SHM / RPMsg / callback
------- 进程或设备边界 -------------------------------------
       |
BridgeEndpoint：系统服务、权限、通知泵、Backend dispatch
       |
RuntimeBackend / GeneratedBackend
```

### Bridge Core 负责

- CALL / RESPONSE / NOTIFICATION 交互；
- route ID、call ID、错误语义；
- 半双工状态机与 RESPONSE 优先级；
- 超时、重试、幂等约束；
- 能力协商；
- 全双工下的请求流水化和主动通知；
- 协议级 credit、订阅和多逻辑 Lane。

### Adapter 负责

- 构造/工厂建立连接，实例提供 `read/write/close` 或等价平台生命周期；
- 字节收发、物理方向切换、MTU 和链路状态；
- UART、Pipe、TCP、SHM、RPMsg、callback 的平台绑定；
- 不解释数据 ID、参数、Probe 或业务错误。

Codec 独立于 Transport。UART 通常组合 OLINK；本机 Pipe 可组合长度前缀；消息型 Transport 可直接透传。

## 3. 能力 Profile

### 3.1 Core Profile

所有完整 Bridge 必须通过：

- `duplex=half`；
- 单 outstanding CALL；
- 主机轮询 Observation；
- 有界帧、超时与重试；
- RESPONSE 优先；
- 适用于 UART、RS-485、单线收发、Pipe、TCP、inproc。

高能力 Transport 也必须通过 Core Profile 测试，保证同一 Session API 可降级运行。

### 3.2 Interactive Profile

在 `duplex=full` 基础上按能力位开启：

- `unsolicited`：Endpoint 可主动发 NOTIFICATION；
- `pipelined_calls`：允许多个 outstanding CALL，按 call_id 匹配乱序 RESPONSE；
- `flow_control`：Observation 使用 credit/window，不以无限缓存代替背压。

全双工不自动等于流水化；每项能力独立协商。

### 3.3 High-Bandwidth Profile

SoC/HLOS 不需要新的“双工模式”，而需要与双工正交的扩展：

- Control RPC：高优先级、可靠、小消息；
- Observation：可丢弃、限流、批量；
- Bulk/Stream：分片、共享内存或零拷贝描述符；
- 多客户端读、单写者 lease；
- 身份认证、权限和重连恢复。

SHM/RPMsg/TCP Adapter 只提供传输能力；Lane 调度、credit、订阅和 ownership 仍属于 Bridge/Endpoint 契约。

## 4. 半双工基线时序

```text
Host                         Endpoint
  | -------- CALL ----------> |
  |                           | dispatch
  | <------ RESPONSE -------- |
  | ---- POLL_OBSERVATION --->|
  | <--- OBSERVATION_BATCH ---|
```

约束：

1. Host 在 RESPONSE 或超时前不发送下一 CALL；
2. Endpoint 处理 CALL 期间不主动抢占链路发送普通 Observation；
3. 重试沿用 call_id；Endpoint 对最近响应保留有界缓存，避免重复写产生副作用；
4. BULK 使用有界分片和逐片确认；
5. 控制响应不能被日志或波形阻塞。

## 5. 当前运行场景映射

| 执行实现 | 执行触发 | 目标统一端点 | Core Profile 是否覆盖 |
|---|---|---|---|
| 动态 Runtime | 声卡回调 | Binary Pipe Endpoint | 是；音频线程与控制线程分离 |
| 动态 Runtime | 主动推进/全速 | Sessionized Runner + Pipe | 是；RUN/STOP 是系统 CALL |
| 动态 Runtime | 主动推进/按现实时间 | Sessionized Runner + Pipe | 是；推进 worker 与 Endpoint 分离 |
| 生成代码 | Windows 声卡回调 | Binary Pipe Endpoint | 是；GeneratedBackend |
| 生成代码 | PC 主动推进 | Sessionized generated host | 是 |
| 生成代码 | MCU/DSP 外部节拍 | UART + OLINK Endpoint | 是；最低目标基线 |
| SoC/HLOS | callback、RPMsg 或服务进程 | Pipe/RPMsg/TCP/SHM | 是；可升级 Interactive/High-Bandwidth |

主动推进宿主必须会话化，不能只有“一次跑完即退出”的生命周期。统一系统命令至少包含 `HELLO / INIT / START / RUN_BLOCKS / STOP / TEARDOWN`。

## 6. 日志

日志是独立宿主服务，不是 Transport 语义：

```text
Structured Log Source -> Log Sink -> Console / File / Ring / Bridge Notification
```

规则：

- 组件实时 `process`、声卡 callback 和 ISR 禁止文件 IO、格式化和阻塞；
- 高级 PC/SoC/HLOS 宿主可用异步 File Sink 落盘，支持时间戳、轮转和过滤；
- MCU 可写固定容量 Ring Buffer，或在 Bridge Observation 窗口低优先级上报；
- 生命周期日志与 Probe/Observation 分开存储和限流；
- 日志拥塞只能增加 dropped 计数，不能反压音频线程；
- 不配置 Sink 时图和 Bridge 必须正常运行。

当前后端把长驻本机/串口会话日志异步写入工程 `logs/`，一次性运行在进程结束后归档 stdout/stderr。

## 7. 当前实现

P0 已完成：

- `bridge.BridgeSession`：统一 CALL、RESPONSE、NOTIFICATION、重试、数据点访问与状态快照；
- `BridgeCapabilities`：半双工默认，全双工流水化显式开启；
- `ByteTransport` 与 `FrameCodec` 接口；
- `OlinkCodec`；
- 串口会话迁移为 `SerialTransport + OlinkCodec + BridgeSession` 薄组合；
- 半双工拒绝未协商的主动 NOTIFICATION，并按 `probe_interval_ms` 由主机串行轮询；全双工才启用设备主动 Probe；
- 半双工单请求与全双工流水化并发测试；
- `AsyncFileLogSink` 及所有当前运行入口的日志归档。
- HLOS `LengthPrefixCodec`、stdio/process、TCP 和本地命名 Pipe Adapter；Transport 工厂可由用户注册或替换。

当前本机 `RtSession` 仍适配历史文本 stdin/stdout 协议，尚未成为 Binary Pipe Endpoint；这是一项明确的迁移中状态，不作为长期兼容接口保留。

## 8. 实施计划

### P1：Endpoint 与能力协商

1. 定义系统 route、HELLO/IDENTITY、能力位、最大帧和 plan/id_map hash；
2. 定义 C ABI `OrpheusAccessBackend` 和 `BridgeEndpoint`；
3. RuntimeBackend 与 GeneratedBackend 通过同一 Endpoint 测试矩阵；
4. 增加同 call_id 幂等响应缓存和 BULK 分片。

### P2：统一本机 Pipe

1. rt_host 与 host_win 接入二进制 Pipe Adapter；
2. FastAPI 本机路径改用 `BridgeSession + PipeCodec + ProcessTransport`；
3. 主动推进动态/生成宿主会话化，实现 RUN_BLOCKS/START/STOP；
4. 删除文本 SET/GET/PROBE 协议和 `RtSession`，不保留兼容层。

### P3：全双工交互

1. HELLO 协商 `unsolicited / pipelined_calls / flow_control`；
2. RESPONSE 和 Control RPC 为最高优先级；
3. Observation 使用 credit、批量和 dropped 统计；
4. 同一测试矩阵覆盖半双工降级与全双工并发。

### P4：SoC/HLOS

1. [x] TCP、stdio/process 与 Windows Named Pipe/POSIX Unix Socket Adapter；
2. [ ] RPMsg Adapter；
3. [ ] SHM 双队列与零拷贝 Observation descriptor；
4. [ ] Control/Observation/Bulk Lane 与 QoS；
5. [ ] 多客户端、lease、认证和权限；
6. [ ] 文件日志轮转、结构化索引与崩溃前 Ring Buffer 导出。

## 9. 验收标准

- 同一 BridgeSession 测试不修改业务操作即可运行于 in-memory、Pipe、UART；
- 所有 Transport 都通过半双工单 outstanding CALL 基线；
- 全双工流水化时 RESPONSE 可乱序但 call_id 匹配正确；
- RuntimeBackend 与 GeneratedBackend 对同一请求返回逐字节一致结果；
- 主动推进、声卡回调和远端设备仅更换宿主/Adapter，不更换控制 API；
- Observation 或日志拥塞不阻塞音频线程；
- 用户替换 Adapter 或 Log Sink 不需要修改图、Backend 或 Bridge Core。
