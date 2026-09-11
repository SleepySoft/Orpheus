---
title: PC 配置好即完整程序
type: HOW
up: '[[00-index]]'
related:
  - '[[bridge-protocol]]'
  - '[[hlos-transport]]'
  - '[[access-bridge]]'
  - '[[execution-model]]'
  - '[[pc-generated-app-audit]]'
tags: [orpheus/how, codegen, bridge]
---

# PC 配置好即完整程序

> 状态：Core Profile 已实现。目标限定为 Windows/PC；DSP 继续使用同一 GeneratedBackend 与嵌入式 Transport。

## 1. 目标

编辑完成的 Orpheus 图应当能一键生成、编译并产出独立 PC 程序：

1. 程序能直连声卡运行，行为与编辑器运行结果一致。
2. 程序不依赖 Python、Node、源码目录或运行中的 Orpheus 服务。
3. 程序内建 Bridge Endpoint，可通过 Pipe、TCP 或其他 Adapter 调参和监控。
4. 编辑器/后端既能主动启动它，也能连接用户手工启动的实例。
5. 工程切换、服务退出、异常断开和手动停止都必须真实结束或标记进程状态。

一句话定义：静态生成图 + miniaudio 设备宿主 + Bridge Endpoint + 随程序发布的运行元数据 = 独立 PC 程序。

## 2. 现状评估

### Bridge 已实现与未实现的分界

“Bridge 已实现”要按层拆开理解：

1. 已实现：Python 主机侧 BridgeSession、CALL/RESPONSE/NOTIFICATION 语义、超时/重试、id_map 和 Probe 语义。
2. 已实现：HLOS 的 stdio/process、TCP、Windows Named Pipe/POSIX Unix Socket Transport，以及 UART 的 OLINK Framing。
3. 已实现：生成 PC exe 内建标准 C Bridge Endpoint，`host_win` 与 `host_cli` 可被 BridgeSession 直接连接。
4. 平台无关的是协议语义、消息路由和会话契约；Endpoint 仍需要平台 transport binding。Pipe/TCP/UART/SHM 的区别只在 Transport 层，不应复制一套业务协议。

协议接入已完成：GeneratedBackend + C Endpoint 使用 compiler 生成的同一身份和 ID map。

更精确地说，生成代码已经包含 Access Backend 材料：

- `orpheus_graph.c` 包含静态图、控制槽、id_map 和 Probe 数据；
- `orpheus_control_message()` 能接收一条二进制消息并返回二进制响应；
- `host_win.c` 通过 `MSG <hex>` 调用它，并用 `PROBE/PROBE_JSON` 上报 Probe。

但这不是标准 Bridge Endpoint：Endpoint 还需要接收二进制帧、解码 Codec、匹配 CALL/RESPONSE、管理订阅和 Transport 状态。当前 `MSG <hex>` 是文本宿主命令对二进制消息的手工包装，`PROBE` 是宿主每 200 ms 向 stdout 打印的行协议。它让 UI 能工作，但不等于内建 Bridge Endpoint。

### 已具备

- generator.py 已生成静态图、组件库、控制参数代码和 host_win.c。
- host_win.c 已有 miniaudio 设备时钟、参数写入、Bulk、Probe 和 STOP。
- FastAPI 已能启动生成 exe，并管理 stdout/stdin 文本实时会话。
- Python BridgeSession 已统一 CALL/RESPONSE/NOTIFICATION、超时、重试和 Probe 语义。
- HLOS 已有 stdio/process、TCP、Windows Named Pipe/POSIX Unix Socket Transport。

### 主要缺口

1. host_win.c 目前是文本 stdio 协议；BridgeSession 使用二进制 Bridge 帧。
2. 生成程序没有标准 Bridge Endpoint、HELLO/IDENTITY、能力协商和图版本校验。
3. /rt/start 只启动本地宿主或连接已有 TCP/Pipe；没有启动生成程序并立即建立 Bridge 会话的统一入口。
4. 进程生命周期仍需要更严格的退出等待、超时升级、工程切换清理和服务退出清理。
5. 生成程序缺少随发布文件一起携带的参数表、Probe 表、hash 和兼容性元数据。
6. 资源文件（例如 WAV）在导出后需要显式复制或解析规则。

## 3. 架构决策

### Endpoint 放在生成程序内

最终不采用 Python proxy 包装生成 exe，因为这会保留 Python 依赖，违背“配置好即完整程序”。

~~~
UI / REST / SDK
      |
      +--> FastAPI lifecycle API --------+
      |                                  v
      +--> BridgeSession ----------> Generated PC App
                                           |
                                           +--> orpheus_graph
                                           +--> miniaudio device host
                                           +--> Bridge Endpoint
                                           +--> app manifest
~~~

编辑器/后端只作为生命周期管理器和 Bridge 客户端；生成 exe 是 Bridge 服务端。

### 两种使用模式

| 模式 | 说明 | 程序所有权 |
|---|---|---|
| Launch Mode | 后端生成、编译、启动 exe，等待 endpoint ready，再建立 BridgeSession | 后端管理的子进程 |
| Connect Mode | 用户手工运行导出的 exe，UI 通过 Pipe/TCP 连接 | 外部进程，UI 只显示真实状态 |

同一 UI 参数面板、Probe 面板和日志面板应跨两种模式复用。

### Transport 与协议

第一版 PC Endpoint 支持：

- stdio：后端作为子进程启动时使用，便于调试。
- local pipe：Windows 推荐，本机不暴露网络端口。
- loopback TCP：跨用户/跨终端调试和远程测试兜底。

Codec 使用现有长度前缀帧。半双工单 outstanding CALL 是第一版基线；全双工 Probe 推送是第二版能力，不改变消息语义。

### Adapter 平台矩阵

Bridge 协议本身平台无关，但 Adapter 是平台绑定层。生成程序应按目标平台编译实际 Adapter；不支持的平台必须生成明确拒绝的 stub，而不是静默禁用。

| Adapter | Windows | Linux/macOS | 嵌入式/RTOS | 生成程序策略 |
|---|---|---|---|---|
| stdio | 支持 | 支持 | 可作为调试通道或 stub | 后端作为父进程启动时默认使用 |
| local pipe | Windows Named Pipe | Unix Domain Socket | stub | 手工运行/Connect Mode 的首选本机通道 |
| loopback TCP | 支持 | 支持 | 可选；有网络栈时支持 | 显式开启，避免默认暴露网络端点 |
| serial/OLINK | 支持 | 支持 | 支持 | 嵌入式部署和远程设备调音使用 |
| SHM/RPMsg | 后续 | 后续 | 平台特定 | 不作为默认 Adapter |

规则：

1. Adapter 由宿主在启动时选择；Bridge Backend 不感知字节来自 stdio、Pipe、TCP 还是 UART。
2. 平台不支持的 Adapter 必须在初始化或 listen/connect 时返回 `ORPHEUS_BRIDGE_ERR_UNSUPPORTED`，并给出明确诊断。
3. Stub 不得申请线程、socket、阻塞队列或实时资源。
4. manifest 的 `transports` 只列出实际编译进程序的 Adapter；UI/API 只启用这些选项。
5. HLOS Adapter 绑定成功后应把 endpoint 写入诊断通道；stdio 可报告 `stdio://`，Pipe/TCP 必须报告实际地址。嵌入式串口通常不知道主机侧的 COM 编号，因此只报告设备身份，不报告主机串口号。
6. 同一程序可以编译多个 Adapter，但每个 Adapter 独立监听/连接，Bridge Backend 与身份校验语义保持一致。

### Adapter 实现路线

#### P1：C Transport 契约与 stdio 重构

先把“帧循环”和“字节来源”拆开：

1. 新增 C Transport/Channel 契约，统一 `read`、`write`、`close`、可选 `shutdown`；
2. 把长度前缀读写从 `bridge_stdio.c` 提取为共享帧循环；
3. stdio 保留现有函数名，内部改为薄 Adapter；
4. 保留 `ORPHEUS_BRIDGE_MAX_MESSAGE` 和 CALL/RESPONSE 语义，不改 Python 协议；
5. `bridge_endpoint_smoke` 继续作为协议回归；新增 frame-loop 单元测试。

这一步不改 CLI、生成器和 UI 行为。

#### P2：Endpoint 公告

Adapter 绑定成功后，在 stderr 输出一行机器可读公告：

~~~text
BRIDGE_READY {protocol:1,pid:1234,endpoints:[stdio://],id_map_hash:...}
~~~

Pipe/TCP 的示例：

~~~text
BRIDGE_READY {protocol:1,pid:1234,endpoints:[winpipe://orpheus-demo-3f2a]}
BRIDGE_READY {protocol:1,pid:1234,endpoints:[tcp://127.0.0.1:51422]}
~~~

停止时输出：

~~~text
BRIDGE_STOPPED {reason:stop}
~~~

规则：

1. stdout 永远只承载 Bridge 二进制帧；公告只写 stderr；
2. 公告只在 startup/shutdown 输出，不进入实时路径；
3. Pipe/TCP 默认只绑定 loopback/本机会话命名空间；
4. 支持 `--endpoint-file` 时，可以原子写入 endpoint JSON，方便手工启动后的 UI 发现；
5. 嵌入式串口不报告主机侧 COM 编号，只报告设备身份和协议能力。

#### P3：Pipe、TCP 和 Stub Adapter

1. Windows Named Pipe Adapter 使用一个活动连接；支持 `CreateNamedPipe`、`ConnectNamedPipe`、`ReadFile/WriteFile`；
2. POSIX Adapter 使用 Unix Domain Socket；`accept` 后使用同一个帧循环；
3. TCP Adapter 默认绑定 `127.0.0.1`，支持 `--port 0`，绑定后公告真实端口；
4. 平台不支持时编译 stub Adapter，`serve/connect` 返回 `ORPHEUS_ERR_UNSUPPORTED`；
5. 生成器把实际 Adapter 源码和 CMake 选择一起写入生成工程；
6. manifest 增加实际可用的 `transports` 字段。

生成程序 CLI 统一为：

~~~text
--bridge stdio
--bridge pipe [--pipe-name NAME]
--bridge tcp [--host 127.0.0.1] [--port 0]
--endpoint-file PATH
~~~

后端启动生成 exe 时显式传 `--bridge stdio`；手工运行建议 `--bridge pipe`；远程调试才使用 `--bridge tcp`。

#### P4：后端 Connect Mode

1. `run_generated` 支持选择 launch transport，默认 stdio；
2. 新增或复用 `/rt/start` 的 `pipe/tcp` 分支连接外部已运行程序；
3. 连接前读取 `orpheus_app_manifest.json` 并校验 `id_map_hash`；
4. `/rt/status` 返回 endpoint、PID（launch-mode）、exit code 和 Bridge ready；
5. Connect Mode 只断开 BridgeSession，不 terminate 外部进程；
6. Launch Mode 沿用 `STOP -> wait -> terminate -> kill`。

#### P5：UI 与验收

1. UI 工具栏增加“连接外部 PC 程序”；
2. Connect 表单接受 endpoint 字符串，并显示 manifest 身份校验结果；
3. 参数面板、Probe 面板、日志面板复用现有 BridgeSession；
4. 启动 Mode 与 Connect Mode 的状态不要互相覆盖；
5. 新增 Pipe/TCP loopback、stub 拒绝、身份不匹配拒绝写、断连和 STOP 生命周期测试。

建议的验收顺序：

1. stdio 行为与当前完全一致；
2. Windows Named Pipe 手工连接成功；
3. POSIX Unix Socket 手工连接成功；
4. loopback TCP 手工连接成功；
5. 不支持平台返回 `ORPHEUS_ERR_UNSUPPORTED`；
6. UI Connect Mode 能读 Probe、写参数并断开；
7. Launch Mode 的旧流程无回归。

## 4. Endpoint 与运行元数据

生成工程应包含 orpheus_app_manifest.json：

~~~json
{
  protocol: orpheus-bridge/1,
  kind: pc-generated-app,
  project_name: demo,
  graph_hash: ...,
  plan_hash: ...,
  id_map_hash: ...,
  sample_rate: 48000,
  block_size: 256,
  transports: [stdio, pipe, tcp],
  routes: [],
  id_map: [],
  assets: []
}
~~~

第一版使用生成目录内的 manifest 文件；后续可内嵌到 exe 或旁路文件。Endpoint 必须在 HELLO/IDENTITY 中上报：

- protocol version；
- graph/plan/id_map hash；
- sample rate 与 block size；
- 参数 route 和 Probe route 能力；
- transport 能力；
- 进程 PID 与启动时间。

主机在 id_map_hash 不一致时禁止写参数；只允许读取身份与状态，避免版本漂移导致误操作。

## 5. 生命周期契约

### Launch Mode

1. 生成工程。
2. 编译 exe。
3. 选定 Pipe/TCP/stdio endpoint。
4. 启动进程。
5. 等待 endpoint ready 和 HELLO。
6. 建立 BridgeSession。
7. 状态进入 connected。
8. 工程切换、后端 shutdown 或手动 stop 时执行统一停止流程。

### 统一停止流程

1. 发送 Bridge STOP。
2. 关闭 transport。
3. 等待进程退出，默认 3 秒。
4. 超时后 terminate。
5. 再等待 2 秒。
6. 仍不退出则 kill。
7. 记录 final exit code。
8. 只有确认退出或标记为 orphan 才更新 UI 状态。

`RuntimeSessionManager` 统一持有 Process/Serial/TCP/Pipe 会话；`ProcessBridgeSession` 记录 PID、启动时间、身份校验和 final exit code，并执行 STOP -> wait -> terminate -> kill。

### 工程切换与服务退出

- 切换工程前强制停止当前会话。
- 后端 shutdown 遍历所有 launch-mode 子进程并执行统一停止。
- Connect Mode 不允许后端杀外部进程，只能断开 BridgeSession。
- Reader 线程感知 EOF/Poll 错误，并把状态改为 exited/crashed。
- UI 轮询状态或由 REST 返回真实进程状态，不用本地假状态。

## 6. REST/UI

当前统一 API：

~~~http
POST /api/projects/{name}/run_generated
POST /api/projects/{name}/rt/stop
GET  /api/projects/{name}/rt/status
GET  /api/projects/{name}/rt/map
~~~

设备图的 `run_generated` 返回 `mode=realtime, status=started`；后续参数、Probe、日志、MAP 和停止与动态 Runtime 共用 `/rt/*`。状态包含：

~~~json
{
  running: true,
  pid: 1234,
  endpoint_ready: true,
  bridge_ready: true,
  exit_code: null,
  bridge: {duplex: half, pipelined_calls: false}
}
~~~

UI 工具栏分为三个入口：

1. 动态运行。
2. 生成并运行 PC 程序。
3. 连接外部 PC 程序。

停止按钮、状态徽标、日志和 Probe 面板统一走 generated app 会话模型，不维护一套并行的假状态。

## 7. 实施阶段

### P0：生命周期闭环（已完成）

1. `RuntimeSessionManager` 管理所有 Bridge 会话。
2. `ProcessBridgeSession` 实现 STOP -> wait -> terminate -> kill。
3. 工程切换、删除和服务 shutdown 清理会话。
4. 状态返回 PID、Endpoint/Bridge ready 与退出码。

### P1：生成 Bridge Endpoint（已完成）

1. 抽出可复用的 C Bridge Endpoint 基础：帧读写、HELLO、CALL dispatch、错误码和 Probe 缓冲。
2. `host_win.c` 与 `host_cli.c` 直接嵌入共享 Endpoint，无文本过渡层。
3. 本机启动使用 LengthPrefix stdio；外部连接支持 TCP/Local Pipe。
4. 生成 orpheus_app_manifest.json。
5. BridgeSession 增加 generated-app HELLO 校验。
6. 用同一测试矩阵验证 stdio/Pipe/TCP。

### P2：统一启动 API 与 UI（Core Profile 已完成）

1. `run_generated` 构建并启动生成设备程序，复用 `/rt/stop|status|map`。
2. 后端负责 endpoint ready、HELLO/IDENTITY 和 manifest hash 校验。
3. UI 接管生成 realtime 会话，并在工程切换前停止旧会话。
4. 参数与 Probe 面板复用统一 ID map/BridgeSession。

### P3：产品化（部分完成）

1. [x] host_win/host_cli 使用共享二进制 Endpoint。
2. [x] 删除文本 RtSession 兼容层。
3. 支持 loopback TCP 和导出包。
4. 支持资源复制与相对路径规则。
5. 支持全双工 Probe 推送与流控。
6. [x] 增加动态路径与生成路径的一致性及真进程 Bridge 回归。

## 8. 测试与验收

功能验收：

1. 同一工程在动态运行和生成程序运行时，参数、控制链路和 Probe 行为一致。
2. 生成 exe 独立复制到另一个目录后仍能运行。
3. Bridge SET 后运行参数真实变化。
4. Bridge GET/PROBE 返回正确数据。
5. STOP 后进程确实退出。
6. 崩溃后 UI 显示退出/崩溃，而不是保持 connected。
7. 工程切换和服务退出后无 launch-mode 残留进程。
8. Connect Mode 断开不影响外部程序运行。

自动化命令：

~~~powershell
python -m pytest orpheus_core/tests/
cd ui; npm test -- --watchAll=false
cd ui; npm run build
cd ui; npm run test:e2e
~~~

新增测试应覆盖：

- C Endpoint 帧编解码。
- HELLO/IDENTITY 与 hash 拒绝写入。
- Pipe/TCP/stdio Bridge loopback。
- generated app lifecycle。
- launch/connect 状态隔离。
- 双路径一致性。

## 9. 风险与取舍

| 风险 | 影响 | 对策 |
|---|---|---|
| C 侧 Pipe/TCP 实现复杂 | 工作量高 | P1 先做 stdio/Pipe，TCP 放后；复用帧协议与 Python 测试矩阵 |
| 二进制 stdout 混入日志 | 帧损坏 | stdout 仅 Bridge 帧，诊断统一写 stderr/File Sink |
| 后台进程残留 | 无法管理和占用声卡 | P0 优先做生命周期闭环 |
| 版本漂移 | 误写不同图参数 | HELLO 校验 hash，不一致只读 |
| 资源路径漂移 | 生成程序找不到 WAV | manifest 记录 assets，构建时复制或校验 |
| 多客户端写参数 | 参数竞争 | 第一版单写者；后续加 write lease |

## 10. 下一切片

下一阶段聚焦订阅式 Observation、同 call_id 幂等缓存、BULK 分片、TLS/lease、多客户端与 SHM/RPMsg 多 Lane。
