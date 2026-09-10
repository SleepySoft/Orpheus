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

> 状态：方案评估与实施计划。目标限定为 Windows/PC；DSP 仍使用既有嵌入式生成路径。

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
3. 未实现：生成 PC exe 内部的标准 C Bridge Endpoint。因此 host_win.c 现在还不能直接被 BridgeSession 连接，它讲的是另一套文本 SET/GET/STOP 协议。
4. 平台无关的是协议语义、消息路由和会话契约；Endpoint 仍需要平台 transport binding。Pipe/TCP/UART/SHM 的区别只在 Transport 层，不应复制一套业务协议。

因此 P1 不是重新设计 Bridge，而是把已有的协议接入生成程序，实现 GeneratedBackend + C Endpoint。

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

RtSessionManager 需要记录 PID、启动时间、transport、manifest hash 和 final exit code。UI 停止按钮只能依据统一 stop API 结果展示“已停止”“超时升级停止”“进程异常退出”“存在孤儿进程，需要系统级处理”。

### 工程切换与服务退出

- 切换工程前强制停止当前会话。
- 后端 shutdown 遍历所有 launch-mode 子进程并执行统一停止。
- Connect Mode 不允许后端杀外部进程，只能断开 BridgeSession。
- Reader 线程感知 EOF/Poll 错误，并把状态改为 exited/crashed。
- UI 轮询状态或由 REST 返回真实进程状态，不用本地假状态。

## 6. REST/UI 演进

新增或扩展 API：

~~~http
POST /api/projects/{name}/generated/app/build
POST /api/projects/{name}/generated/app/start
POST /api/projects/{name}/generated/app/stop
GET  /api/projects/{name}/generated/app/status
~~~

start 请求示例：

~~~json
{
  transport: pipe,
  mode: launch,
  pipe_address: orpheus-demo,
  host: 127.0.0.1,
  port: 0,
  audio: {
    device: default,
    sample_rate: 48000,
    block_size: 256,
    period_ms: 10
  },
  auto_stop_on_project_switch: true
}
~~~

status 返回：

~~~json
{
  mode: launch,
  state: connected,
  pid: 1234,
  transport: pipe,
  endpoint_ready: true,
  bridge_ready: true,
  manifest: {graph_hash: ...},
  last_probe_at: 0,
  exit_code: null,
  last_error: null
}
~~~

UI 工具栏分为三个入口：

1. 动态运行。
2. 生成并运行 PC 程序。
3. 连接外部 PC 程序。

停止按钮、状态徽标、日志和 Probe 面板统一走 generated app 会话模型，不维护一套并行的假状态。

## 7. 实施阶段

### P0：生命周期闭环

先不等待二进制 Endpoint 全量完成，优先消除“后台还在跑但界面管不了”的问题。

1. 扩展 RtSessionManager 的状态、PID、退出码和异常记录。
2. 实现统一 STOP -> wait -> terminate -> kill。
3. 工程切换、关闭服务和 UI 重载前清理 launch-mode 会话。
4. 增加 orphan 检测和状态查询。
5. UI 显示真实运行/崩溃/停止状态。
6. 增加崩溃、EOF、工程切换和服务 shutdown 测试。

### P1：生成 Bridge Endpoint

1. 抽出可复用的 C Bridge Endpoint 基础：帧读写、HELLO、CALL dispatch、错误码和 Probe 缓冲。
2. 新增生成宿主 host_bridge.c，保留现有 host_win.c 作为过渡。
3. 第一版实现 stdio 与 local pipe。
4. 生成 orpheus_app_manifest.json。
5. BridgeSession 增加 generated-app HELLO 校验。
6. 用同一测试矩阵验证 stdio/Pipe/TCP。

### P2：统一启动 API 与 UI

1. 新增 generated app build/start/stop/status API。
2. 后端负责 endpoint ready、HELLO 和 manifest 校验。
3. UI 增加生成程序和外部程序两种模式。
4. 参数面板直接由 manifest/id_map 驱动。
5. Probe 面板展示更新时间和断连状态。

### P3：产品化

1. host_bridge.c 替代 host_win.c 文本协议。
2. 删除临时文本 RtSession 兼容层。
3. 支持 loopback TCP 和导出包。
4. 支持资源复制与相对路径规则。
5. 支持全双工 Probe 推送与流控。
6. 增加动态路径与生成路径的一致性回归。

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
| 文本协议与 Bridge 并存 | 状态混乱 | 只把 host_win.c 当过渡，P3 删除 |
| 后台进程残留 | 无法管理和占用声卡 | P0 优先做生命周期闭环 |
| 版本漂移 | 误写不同图参数 | HELLO 校验 hash，不一致只读 |
| 资源路径漂移 | 生成程序找不到 WAV | manifest 记录 assets，构建时复制或校验 |
| 多客户端写参数 | 参数竞争 | 第一版单写者；后续加 write lease |

## 10. 首个落地切片

建议第一个 PR 只做 P0：

1. RtSessionManager 提供真实状态和统一停止。
2. /rt/status 明确返回 PID、running、exit code 和 last_error。
3. 工程切换与服务退出清理会话。
4. UI 停止按钮等待后端确认。
5. 不改音频行为，不引入 Bridge Endpoint。

先用最小改动解决当前最痛的失控进程问题，再进入 Bridge Endpoint 的结构性改造。
