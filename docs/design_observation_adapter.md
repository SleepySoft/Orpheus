# 观测点与外部 Adapter 设计

> 状态：设计定案（v1）；第一阶段的生成图模块/宿主解耦已落地，观测点迁移按本文后续阶段实施。

外部访问、会话、传输与动态 Runtime 的统一架构见 `design_access_bridge.md`；本文只定义观测点和观测 Adapter 的数据面边界。

## 1. 目标与边界

生成代码的图本体只负责确定性音频处理、状态与调度，不负责界面、串口、JSON、文件或日志。

- **观测点（Observation Point）**是图中可被外部读取的数据位置，不是必须执行的算法节点。
- **观测 Adapter**负责采样、降频、快照、编码和传输，可按平台选择串口、共享内存、调试器内存窗口或其它机制。
- UI 只消费统一的观测数据，不感知底层传输。
- 未选择 Adapter 时，观测点不产生 IO，不阻塞实时线程，也不要求生成宿主输出任何文本。
- 算法输出若参与控制链或下游计算，则仍是图算法的一部分，不能作为“仅观测”裁剪。

## 2. 生成工程分层

```text
用户 main / 音频中断
        |
        v
include/orpheus_graph.h        唯一图入口与结构化错误
src/orpheus_graph.c            状态、Buffer、初始化链、调用链、销毁链
        |
        +-- src/orpheus_control.c / id_map.c   数据点读取与控制
        +-- 可选 observation adapter           UART/共享内存/自定义传输

src/main.c                     最小集成示例，可删除
src/host_cli.c                 PC 验证宿主，不属于产品图本体
src/host_win.c                 Windows 声卡宿主，不属于产品图本体
```

图入口保持简单：

```c
int orpheus_graph_init(uint32_t sample_rate, uint32_t block_size);
int orpheus_graph_process(uint32_t frame_count);
int orpheus_graph_process_task_<id>(uint32_t frame_count);
void orpheus_graph_teardown(void);
const OrpheusGraphError* orpheus_graph_last_error(void);
```

实现继续导出 `orpheus_generated_*` 二进制符号以兼容既有宿主，正式头用零开销宏提供上述推荐名称。

`orpheus_graph.c` 禁止宿主 IO。初始化/处理失败只返回错误码并记录 `operation/node/component/code`，由用户 main 或 Adapter 决定如何上报。
`orpheus_graph_process()` 每次只推进一个编译期 `ORPHEUS_GRAPH_TICK`；若硬件回调帧数与图块长不同，宿主必须像现有 rt_host/host_win 一样分块调用，不能把任意长度直接传入。

## 3. 两类观测

### 3.1 状态观测

参数、状态、RMS、进度等已有注册槽继续使用 32 位数据 ID。外部在非实时上下文通过控制接口读取：

- `orpheus_control_probe_count()` / `orpheus_control_probe_get()`；
- `orpheus_control_get_value_id()`；
- §18 消息信封的 READ 请求。

`uart_link` 已是该模式的第一个 Adapter：它是 `execution.none` 声明节点，不进音频调用链；生成后在 `poll(now_ms)` 中读取 PROBE 槽并经 OLINK 上报。

### 3.2 音频信号观测

工程后续新增顶层 `observations`，直接引用已有输出端点，而不是插入直通 probe 节点：

```yaml
observations:
  - id: post_bass
    from: bass:out
    format: audio_f32
    policy:
      decimation: 16
      max_frames: 128
```

编译器为观测点生成稳定 ID 和只读描述符。图处理完成后，Adapter 可获得该块的借用视图；数据有效期到下一次对应 Task 处理，默认不复制。

建议 C 接口：

```c
typedef struct OrpheusObservationView {
    uint32_t id;
    const void* data;
    uint32_t frames;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t sequence;
} OrpheusObservationView;

size_t orpheus_observation_count(void);
int orpheus_observation_get(size_t index, OrpheusObservationView* out);
```

Adapter 若跨线程或跨中断使用，必须自行复制到固定容量 SPSC/双缓冲；满时丢弃并计数，绝不阻塞音频线程。

## 4. Adapter 模型

Adapter 使用声明组件接入，均为 `execution.none: true`，不进入音频拓扑：

- `uart_link`：已有，OLINK + §18 消息 + 周期探针泵；
- `observation_uart`：发送选定音频观测点，负责降采样、量化和限流；
- `observation_shm`：PC 本地共享内存/环形缓冲；
- `observation_callback`：生成弱符号或用户回调模板，交给已有 RTOS/IPC；
- `observation_none`：只保留 ID/描述符，不生成传输实现。

Adapter 参数负责选择观测点、采样策略与带宽预算。图算法源码不包含协议和 UI 逻辑。
Adapter 生命周期必须提供对称的 `init/deinit`；图模块只管理算法节点，平台资源由宿主在 graph init/teardown 外围显式打开和关闭。

## 5. 现有 Probe 组件迁移

现有组件不能一次性删除，需要区分语义：

1. **纯直通观测**：`probe_waveform`、`probe_rms` 等若结果只给 UI/外部读取，可迁移为 `observations + Adapter`，生成部署代码时不再复制该 probe 组件，并把其音频输入/输出直连折叠。
2. **派生分析**：频谱、扫频记录等需要计算。计算器可移到 Adapter，或保留为可选 observation processor；默认部署 profile 不进入主图。
3. **参与控制**：若 probe/state 输出通过 `control_connections` 驱动算法参数，它已参与运行语义，必须保留为算法节点，不能裁剪。
4. **显式保留**：教学、PC 分析或设备自诊断场景可选择 `observability: embedded`，把观测计算编入目标。

建议生成 profile：

- `observability: none`：删除纯观测节点与全部 Adapter；
- `observability: metadata`（默认）：保留观测点描述符，不带传输；
- `observability: embedded`：带用户选定的计算器和 Adapter。

## 6. UI 与后端

后端把本地 Runtime、串口、共享内存等统一为现有 ControlPlane 形状：

```text
{ node, observation/id, value, sequence, dropped, timestamp }
```

UI 的波形、电平和频响控件只订阅观测 ID。断链、限流或丢帧显示为观测状态，不影响图运行。

## 7. 实施顺序

1. **已完成**：生成物拆为 `orpheus_graph` 静态库、最小 `main.c`、独立 `host_cli.c`；核心处理路径无 stdio。
2. 增加 `observations` schema、编译校验、稳定 ID 与只读 buffer view API。
3. UI 将“观察端点”保存为 `observations`，先由本地 Runtime Adapter 实现。
4. 扩展 `uart_link` 或新增 `observation_uart`，传输音频观测块并接入现有 SerialSession。
5. 给纯观测 probe 增加迁移 pass；控制链引用的节点禁止裁剪。
6. 增加带宽预算、丢弃计数、SPSC 快照和动态/生成路径一致性测试。

## 8. 验收标准

- 删除所有 Adapter 后，`orpheus_graph.c` 不含串口、文件、printf、JSON 或线程 API。
- 用户自有 main 仅需 include `orpheus_graph.h` 并调用 init/process/teardown。
- 同一观测 ID 可经本地、UART 或共享内存显示在同一 UI 控件中。
- Adapter 阻塞或断开不能阻塞音频处理；只能丢观测数据并累计计数。
- `observability: none` 下纯观测节点不进入组件源码、arena、Buffer 和调用链。
- 参与控制语义的观测计算不会被错误裁剪。
