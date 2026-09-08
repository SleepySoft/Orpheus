# Access Bridge 统一访问桥设计

> 状态：设计定案（v1）；顶层 `bridges`、画布配置节点、主机 BridgeSession 核心及 `uart + olink` 已落地。双工 Profile、能力和日志契约见 `design_bridge_protocol.md`。本文的 Access Bridge 专指控制/观测访问桥，与音频 Task 间的 `async_bridge` 无关。

“访问端点”是运行三轴层级模型中的一个维度，只决定 UI/SDK 连到哪个实例；它不决定执行实现、执行触发、主动推进 pacing 或图时间线。定义见 `design_execution_model.md` 与 `design_timeline.md`。

## 1. 问题

系统内部已经具备完整的访问能力：

- 编译期 `plan.id_map` 与稳定 32 位数据 ID；
- 动态 Runtime 的 `message/read_id/write_id/read_bulk/resolve`；
- 生成图库的 `orpheus_control_message/get_value/get_bulk/probe_get`；
- 本地 rt_host 文本管道和远程 OLINK 串口。

当前缺口不是内存或访问 API，而是外部接入仍按宿主分别实现：本地 `RtSession` 解析文本命令，远程 `SerialSession` 解析 §18 二进制消息。两条路径虽然功能相近，但会逐渐产生能力和错误语义差异。

目标：UI、脚本和自动化只面对一个 ControlPlane/BridgeSession；动态 Runtime、生成代码、UART、进程管道、共享内存和进程内调用共享同一访问语义。

## 2. 分层

```text
UI / REST / Python SDK
          |
          v
BridgeSession（CALL 匹配、订阅、重连、缓存、权限）
          |
          v
Frame Codec（datagram 直通 / OLINK COBS+CRC / 共享内存槽）
          |
          v
Transport Adapter（inproc / pipe / UART / USB / TCP / SHM）
========== 进程或设备边界 ====================================
          |
          v
Bridge Endpoint（握手、能力、订阅、通知泵、限流）
          |
          v
Access Backend（唯一语义入口）
    ├── RuntimeBackend：Runtime::message / resolve_all
    └── GeneratedBackend：orpheus_control_message / id_map
```

每层只负责一件事：

- **Access Backend**：按 ID 读写、BULK、消息分发、观测枚举；不感知 UART/TCP/UI。
- **Bridge Endpoint**：管理连接、订阅、通知与权限；不直接理解组件内存布局。
- **Frame Codec**：解决字节流边界和校验；消息型 transport 可直接透传。
- **Transport Adapter**：只负责收发字节或帧。
- **BridgeSession**：主机侧统一 API，供 UI/REST/SDK 使用。

## 3. Access Backend 契约

Bridge 不直接读裸地址。动态 Runtime 和生成图库都适配为同一后端：

```c
typedef struct OrpheusAccessBackend {
    void* context;
    int (*dispatch)(void* context,
                    const uint8_t* request, size_t request_len,
                    uint8_t* response, size_t response_cap, size_t* response_len);
    const OrpheusIdEntry* (*map)(void* context, size_t* count);
    int (*observation_get)(void* context, size_t index,
                           OrpheusObservationView* out);
} OrpheusAccessBackend;
```

- RuntimeBackend 的 `dispatch` 调 `Runtime::message()`；
- GeneratedBackend 的 `dispatch` 调 `orpheus_control_message()`；
- read/write/BULK/CUSTOM 均继续使用 §18 消息信封，不增加第二套操作协议；
- `map` 是元数据，不向远程暴露进程地址。`base_ptr/offset` 只允许 inproc/debugger adapter 本地查看。

## 4. Bridge 系统服务

数据点 ID 仍直接作为 §18 `route_id`。Bridge 自身服务使用保留的系统路由空间，例如 `CUSTOM/module=0xFF`：

- `HELLO`：协议版本、ABI、端序、最大帧、能力位；
- `IDENTITY`：工程/plan hash、id_map hash、固件 build id；
- `MAP`：可选分块读取 ID map；
- `SUBSCRIBE` / `UNSUBSCRIBE`：观测 ID、周期、降采样、批大小；
- `STATS`：队列水位、丢帧、CRC 错、超时、重连次数；
- `LEASE`：可选单写者租约，多客户端默认只读。

主机连接后先校验 `id_map hash`。UI 缓存的工程 map 与设备不一致时禁止写入，只允许显示“固件/工程不匹配”，避免正确 ID 被错误固件解释。

## 5. Transport Adapter

Adapter 不改变消息语义，只实现传输：

所有完整 Adapter 必须支持双向半双工 Core Profile；全双工仅按能力升级主动通知和请求流水化。真正单工只允许显式的只读遥测或无确认写入子集。双工状态机、协议级流控和多 Lane 属于 Bridge，不得塞入 Transport。完整约束见 `design_bridge_protocol.md`。

| Adapter | 边界 | Framing | 用途 |
|---|---|---|---|
| `inproc` | 同进程 | 无 | 单测、桌面一体化、直接嵌入 Runtime |
| `pipe` | 本机子进程 | 长度前缀或 OLINK | UI 后端 ↔ rt_host / generated host |
| `uart` | MCU/SoC 串口 | OLINK | 设备调音与低带宽观测 |
| `usb_cdc` | USB 字节流 | OLINK | 比 UART 更高带宽，语义不变 |
| `tcp` | 网络 | 长度前缀/TLS | 实验室远程设备 |
| `shm` | 本机共享内存 | SPSC 槽 | 高频波形/音频观测 |
| `callback` | 用户平台 | 用户保证帧边界 | RTOS mailbox、厂商 IPC、自有驱动 |

工程顶层 `bridges` 是部署配置的唯一事实来源。UI 将其投影为 `execution.none: true`、无端口的「访问桥」配置节点；保存时节点抽回顶层，不进入 `graph.nodes`。它们不进入音频拓扑和 `orpheus_graph_process()` 调用链，只给生成项目增加 endpoint/transport 文件及平台钩子。

```yaml
bridges:
    - id: debug_uart
        transport: uart
        codec: olink
        enabled: true
        params:
            resource: uart2
            baud: 921600
            probe_interval_ms: 200
        position: {x: 80, y: 320}
```

旧 `orpheus.builtin.uart_link` 节点由 loader/compiler 兼容映射为同一标准 Bridge；首次经 UI 保存后迁移到顶层 `bridges`。

画布统一使用 `orpheus.builtin.access_bridge`，`transport` 参数选择 Adapter。当前 UI/生成器仅开放已实现的 `uart + olink`；未安装的 transport 在编译期明确报错，不生成半成品。既有 `uart_link` 仅作为兼容入口。

### 5.1 两层选择，不手改生成代码

界面中有两个不同层次的选择：

1. **工程 Bridge 节点**配置目标侧逻辑资源：`transport=uart`、`resource=uart2`、codec、波特率和观测周期。这些配置进入顶层 `bridges`，决定生成哪些 Endpoint/Adapter 文件。
2. **运行工具栏目标**选择主机本次连接的实际端点：例如 `COM5`。它是本机环境状态，不写进可移植工程。

`resource: uart2` 到 STM32 HAL、TI DriverLib 或 Linux termios 的绑定由 Target Profile 提供。一个平台 Adapter 包实现一次，所有工程复用；只有未知/自定义平台才回退到生成 callback 桩，由用户 main 注册函数指针，而不是修改 `orpheus_graph.c` 或每次覆盖的生成文件。

## 6. 控制面与观测面的带宽分离

同一 Bridge 可承载两类逻辑通道，但必须分别限流：

1. **Control RPC**：低带宽、可靠、CALL/RESPONSE、超时重试；参数写、BULK、MAP、CUSTOM。
2. **Observation Stream**：高频、允许丢弃、NOTIFICATION；标量 probe、状态、波形或音频观察点。

UART 可以复用一条 OLINK 物理链路，但 Control RESPONSE 优先于 Observation。SHM/TCP 可将 observation 放独立队列或 socket，避免大波形阻塞调参。

观测发送分两阶段：

```text
音频线程/中断：graph_process -> bridge_capture（定长复制/入 SPSC，满则丢）
低优先级任务：bridge_poll -> 编码 -> transport.send
```

- `bridge_capture` 只处理工程显式订阅的点，操作上界在编译期确定；
- 不做 JSON、printf、阻塞锁或驱动 IO；
- `bridge_poll` 才做分片、压缩、CRC 和发送；
- 标量 PROBE 可由 poll 直接读取注册槽，无需每块 capture；
- 中间音频 Buffer 的借用视图只在下一次对应 Task 处理前有效，跨线程必须复制。

## 7. 与动态 Runtime 的有机结合

### 7.1 本地运行（目标架构）

目标是 rt_host 内创建 `RuntimeBackend + BridgeEndpoint`，后端进程使用 `PipeTransport` 的二进制帧，Python 使用统一 `BridgeSession`。当前本机仍由文本 `RtSession` 适配；项目处于开发阶段，二进制 Pipe 落地后直接删除该文本协议，不保留兼容 shell。

```text
UI -> BridgeSession -> PipeTransport -> rt_host BridgeEndpoint -> Runtime::message
```

Runtime 的音频线程保持不变。Endpoint 在控制线程处理 CALL；probe/observation pump 在低优先级线程读取快照并发送 NOTIFICATION。

### 7.2 生成代码在 PC 运行

`orpheus_graph` 使用 GeneratedBackend；`host_win` 或 `host_cli` 只选择 Pipe/TCP/stdio Adapter。UI 使用同一 BridgeSession，不需要知道目标是动态 DLL Runtime 还是静态生成图。

### 7.3 远程设备

设备侧 GeneratedBackend 不变，只把 Adapter 换成 UART/USB/TCP。主机侧 BridgeSession 的 API、ID map、订阅和 UI 数据形状与本地完全相同。

```text
UI -> BridgeSession -> SerialTransport/OLINK -> device Endpoint -> orpheus_control_message
```

因此“直接使用 Runtime 运行”不是另一套模式，而是 Access Bridge 的一个 Backend + Transport 组合。

## 8. 主机侧统一接口

P0 已实现统一 Python `BridgeSession`，串口路径已迁移；当前文本 `RtSession` 将随二进制 Pipe Endpoint 落地后删除。稳定接口为：

```python
class BridgeSession:
    def call(self, route_id, payload=b''): ...
    def read(self, data_id): ...
    def write(self, data_id, value): ...
    def read_bulk(self, data_id): ...
    def write_bulk(self, data_id, values): ...
    def subscribe(self, observation_ids, policy): ...
    def snapshot(self): ...
    def map_all(self): ...
    def close(self): ...
```

`ByteTransport` 只需 `read/write/close`（构造/工厂负责打开）；OLINK 是可组合 Codec，不写进 BridgeSession。REST `/rt/*` 和 UI 保持现有形状，由 Session 工厂选择 local-pipe、serial、shm 等实现。

## 9. 多 Bridge 与权限

- 一个 Endpoint 可挂多个 Bridge：例如 UART 调参 + SHM 波形 + debugger memory map；
- 每个 Bridge 有独立订阅、带宽预算和统计；
- 默认允许多读者、单写者；写权限可按 ID kind/module 白名单限制；
- PROBE/STATE 永远只读，RTC/TUNE 写入继续服从现有类型、范围和双 bank 规则；
- 断链只清该 Bridge 的订阅和租约，不影响图运行。

## 10. 生命周期

Adapter 契约必须对称：

```c
int  orpheus_bridge_<name>_init(const OrpheusAccessBackend* backend);
void orpheus_bridge_<name>_capture(uint32_t task_id);  /* 可选、RT bounded */
void orpheus_bridge_<name>_poll(uint32_t now_ms);      /* 非实时 */
void orpheus_bridge_<name>_deinit(void);
```

`orpheus_graph` 不自动执行阻塞 poll。用户 main/RTOS 负责在合适上下文调度 poll；生成的最小 main 不默认携带 Bridge。

## 11. 实施顺序

1. [x] Python 抽出 BridgeSession、Codec、Transport；串口迁移到半双工 Core Profile；
2. [x] 增加可选异步文件 Log Sink，运行日志与实时处理解耦；
3. [ ] 定义 `OrpheusAccessBackend` 与 Bridge 系统服务路由、HELLO/IDENTITY/hash；
4. [ ] 用 GeneratedBackend 包装现有 `orpheus_control_message`，将 UART 链路改造为正式 Endpoint + UartTransport；
5. [ ] 实现 RuntimeBackend，rt_host/host_win 增加二进制 PipeTransport，删除文本协议；
6. [ ] 主动推进动态/生成宿主会话化，统一 START/RUN_BLOCKS/STOP 生命周期；
7. [ ] 接入 `observations` 与 subscribe/capture/poll；先标量 probe，再音频 Buffer view；
8. [ ] 增加 RPMsg/TCP/SHM/callback、多 Lane、写租约与故障统计。

## 12. 验收标准

- 同一个 Python BridgeSession 测试矩阵可无修改跑过 inproc、pipe、UART loopback；
- 同一 §18 请求对 RuntimeBackend 和 GeneratedBackend 返回逐字节一致 RESPONSE；
- UI 在本地动态 Runtime、PC 静态生成图和串口设备间切换时不改变参数/观测代码；
- 工程/id_map hash 不一致时拒绝写操作；
- Observation 拥塞只能增加 dropped 计数，不能增加音频线程阻塞时间；
- 不带 Bridge Adapter 的生成图不包含传输、线程、printf、JSON 和平台驱动。
