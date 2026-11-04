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

当前单帧 payload 上限为 1023 个 32 位 word。数值 BULK 已支持能力协商后的
事务分片；主机 `read_bulk/write_bulk` 自动拆分，不更换 OLINK 或长度前缀 Codec。
主动 NOTIFICATION 不分片；内置 Probe 仍保持单帧有界，结构化 STRING Probe
必须证明最坏编码长度不超过上限。当前按声明容量审计的超限槽如下：

| 组件 | 槽及元素容量 |
|---|---|
| `bulk_relay` | `values_storage`：2048 |
| `fir_bank` | `coefs`：4096；`coefs_banked`、`coefs_per_channel`：32768 |
| `iir` | `coefs_per_channel`：2112 |
| `iir_bank` | `coefs_per_channel`：5120 |
| `input_mixer_3d` | `weights`：1024 |
| `matrix_mul` | `matrix`：1024 |
| `rnc_mimo_nlms` | `weights`：12000 |
| `slc_matrix_mul` | `requested_gains`、`active_gains`、`accepted_targets`：1024 |

不得缩短组件容量冒充协议支持。不支持分片的端点仍明确拒绝超限数组；
不带分片标记的超限读取仍返回 ERROR，不截断数组或返回成功空值。
1023 word 的限制属于消息头，不随调用者提供的大缓冲放宽：动态 Runtime、
生成代码、hook RESPONSE 和 Python 消息助手均须遵守；输入帧实际长度必须与头部一致。

### 3.4 BULK 分片事务

HELLO 能力 `BULK_FRAGMENT = 1 << 7` 表示支持以下扩展。原8字节消息头不变，
`flags` 窗口的 bit0（头部 bit26）为 `FRAGMENT`，route_id 仍为数组 ID。
每片 payload 先放5个 LE uint32：

| 字段 | 含义 |
|---|---|
| operation | READ=1、WRITE=2、COMMIT=3、CANCEL=4 |
| transfer_id | 会话内非零事务 ID；与单次 RPC 的 call_id 独立 |
| offset | 以元素为单位的起始位置 |
| count | 本片元素数 |
| total | 本事务总元素数 |

数值元素为 float32/int32，单片最多1018个元素，实际还受协商帧长限制。
成功响应回显片头；READ 后附 count 个元素，其余操作无附加数据。
ERROR 响应回显片头并附一个 uint32 错误码：1参数无效、2传输不存在、
3忙/等待边界、4冲突、5未收齐、6资源上限、7已结束、8过期。
解析错误不能把片头当普通数组写入。分片端点的帧长至少为32字节。

- **读快照**：READ(offset=0,count=0,total=0) 开启快照并查询实际注册长度，
  响应 total 为该长度（声明容量可能更大）。首次读取可能返回 BUSY，直到所属 Task
  的块边界完成整数组快照；后续 READ 任意顺序取片，整个事务固定同一快照。
  也支持以非零 total 和具体范围直接开启读取；读取结束用 CANCEL 释放快照。
- **写暂存**：首个 WRITE 创建独立暂存区和覆盖位图，不改变算法内存。
  允许不同数组/事务交错，允许乱序与重叠片；已覆盖元素逐字节相同则幂等，
  不同则整片报冲突，不接受半片。route、方向和 total 不得中途改变。
  同一数组（包括内存重叠的别名）只允许一个写事务；普通消息写入也受此独占约束。
- **整组提交**：收齐后显式 COMMIT，未收齐返回错误5。提交请求通过无锁邮箱
  发布，在所属 Task 下一块开始、算法执行前整组应用；控制面获得完成确认前返回 BUSY。
  不在实时线程分配、释放、加阻塞锁或执行链路 IO。即使工程 `double_bank=off`，
  分片写仍单独暂存并在边界提交，不退化为逐片直写。
  提交同时同步已有影子区并清除旧 pending，防止后续旧 bank 覆盖新数组。
- **幂等与取消**：完成的 COMMIT 可重试，不重复应用；CANCEL 也可重试。
  CANCEL 的 total=0 可用于取消首次响应丢失、尚不知道实际长度的读取。
  已应用的提交不能取消，返回错误7，不伪装成回滚成功。
- **有界资源**：每端点最多8条活动传输，共用1 MiB暂存预算（含写覆盖位图）；
  超限明确报错，不降低算法容量。空闲30秒过期；待边界执行的任务也有30秒期限，
  Task 停顿后恢复不能应用过期写入。回收在后续控制请求或端点销毁时执行，
  实时线程只标记完成/过期。完成墓碑在同一有界表内按最近使用保留，
  空闲30秒或表满时淘汰；淘汰后重试 COMMIT 返回“不存在”，绝不重新应用。
  客户端在同一会话内不复用 transfer_id；新 HELLO 清理上一会话。

`orpheus_bridge_endpoint_process` 由一个控制执行者串行调用，半双工仍只允许一个
outstanding CALL，但多个事务可在不同 CALL 之间交错。Task 侧只处理自己的邮箱请求；
端点销毁前必须停止并等待所有 Task。裸 `Runtime::message/orpheus_control_message`
不协商该扩展，拒绝带分片 flag 的输入；统一 Bridge Endpoint 负责分片状态机。
嵌入式自定义 Backend 可提供 `now_ms` 单调时钟，须线程安全且实时安全；
Windows/POSIX 默认使用单调时钟，裸 DSP 默认使用平台 `clock()` 时间基。
Python 的每个边界等待默认30秒，可用 `bulk_timeout` 调整；失败后发送 CANCEL，
清理失败明确记录日志并由端点超时回收。

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

Core Profile 已完成：

- `bridge.BridgeSession`：统一 CALL、RESPONSE、NOTIFICATION、重试、数据点访问与状态快照；
- `BridgeCapabilities`：半双工默认，全双工流水化显式开启；
- `ByteTransport` 与 `FrameCodec` 接口；
- `OlinkCodec`；
- 串口会话迁移为 `SerialTransport + OlinkCodec + BridgeSession` 薄组合；
- 半双工拒绝未协商的主动 NOTIFICATION，并按 `probe_interval_ms` 由主机串行轮询；全双工才启用设备主动 Probe；
- 半双工单请求与全双工流水化并发测试；
- `AsyncFileLogSink` 及所有当前运行入口的日志归档。
- HLOS `LengthPrefixCodec`、stdio/process、TCP 和本地命名 Pipe Adapter；Transport 工厂可由用户注册或替换。
- C99 `BridgeEndpoint`：HELLO、IDENTITY、STOP、STATS、分页 MAP 与 Backend dispatch；
- RuntimeBackend 与 GeneratedBackend；动态 `rt_host`、主动推进 Runtime、生成 `host_win/host_cli` 均使用二进制 stdio；
- compiler 单点生成 graph/plan/id_map hash，BridgeSession 校验失败时禁止写入；
- 生成 CLI 在服务 Endpoint 时并发执行图，STOP 后退出并回收进程。

当前半双工观测由主机按 ID map 轮询；主动 NOTIFICATION、订阅、幂等响应缓存和流控仍属于后续能力升级。

## 8. 实施计划

### P1：Endpoint 与能力协商

1. [x] 定义系统 route、HELLO/IDENTITY、能力位、最大帧和 plan/id_map hash；
2. [x] 定义 C ABI `OrpheusBridgeBackend` 和 `BridgeEndpoint`；
3. [x] RuntimeBackend 与 GeneratedBackend 通过同一 Endpoint 测试矩阵；
4. 增加同 call_id 幂等响应缓存和 BULK 分片。

### P2：统一本机 Pipe

1. [x] rt_host 与 host_win 接入 LengthPrefix 二进制 stdio；
2. [x] FastAPI 本机路径改用 `BridgeSession + LengthPrefixCodec + ProcessTransport`；
3. [x] 主动推进动态/生成宿主会话化并实现 STOP；RUN_BLOCKS/START 留给远程任务扩展；
4. [x] 删除文本 RtSession/host helper，不保留兼容层。

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
