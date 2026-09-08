# 音频时间线模型设计

> 状态：现状审计 + 演进定案（v1）。本文定义图时间、执行触发、跨域同步、延迟和事件时间；运行入口术语见 `design_execution_model.md`。

## 1. 时间线是图的第一等语义

音频系统不能只知道“节点按什么顺序调用”，还必须知道：

- 当前处理的是时间线上的哪一段样本；
- 该段属于哪次连续运行（epoch）；
- 不同 Task/速率域如何映射到共同时间；
- 数据是否发生 seek、reset、丢帧或不连续；
- 一条路径累计了多少算法和缓冲延迟；
- 外部事件应该落在哪一帧或块内偏移；
- 独立设备时钟发生漂移时如何吸收或校正。

统一原则：**样本位置是事实，秒数是派生显示；墙钟只用于执行触发和外部对齐，不能替代图时间。**

## 2. 与执行触发的边界

运行模型使用三个顶层维度：执行实现、执行触发、访问端点。

```text
执行触发
├── 外部节拍触发：声卡回调 / DMA ISR / 硬件 Timer
└── 主动推进：Runtime 或用户循环主动调用 process
    └── pacing：全速 / 按墙钟
```

图时间线与上述触发方式正交：同样的 240,000 个 48 kHz 样本，无论由声卡用 5 秒触发、由 CPU 在 0.1 秒内全速推进，还是由主机按墙钟用 5 秒推进，其图时间都严格为 5 秒。

### 2.1 图内时间根不等于执行触发器

manifest 的 `clock_source` / `clock_domain` 表示图内数据时间根和域兼容关系。例如：

- `wav_in`：file 时间根；
- `signal_gen` / `sweep_gen`：synthetic 时间根；
- `device_in`：device 时间根；
- `embed_in`：embed 时间根。

执行触发回答“谁调用下一次 process”。例如 `device_out` 不是图内数据源，却可由播放回调触发执行。因此不能用一个 `clock_source` 字段同时表达两层语义。

### 2.2 pacing 不属于 source 参数

`pacing` 是主动推进器的本次运行策略，不放入 `wav_in/signal_gen` 等 source：

- 同一图无需改参数即可在全速测试与按墙钟观察间切换；
- 多 source 图没有唯一参数归属；
- source 可能位于复用子组件；
- MCU/DSP 的外部 ISR 不消费 PC pacing；
- pacing 不改变帧数、DSP 结果或图时间，只改变墙钟耗时。

若未来保存默认值，应使用工程级宿主配置（如 `host_clock.pacing`）；当前保持为 UI/API 运行会话选项。

## 3. 当前已实现能力

### 3.1 图速率和端口签名

- 工程/Task 有 `sample_rate`、`block_size`；
- 端口可声明 sample rate、block size 和参数表达式；
- 连接编译期校验采样率、块长、格式和通道；
- 时钟源采样率不一致会报错。

### 3.2 静态调度

compiler 生成：

```text
plan.schedule.tick
plan.schedule.periods[node]
node_configs[node].frames / sample_rate / period
```

节点触发间隔换算到图速率帧，运行时按 `(counter + 1) % period == 0` 触发。跨速率合流使用显式 rate bridge 和 LCM 缓冲。

### 3.3 多 Task 和异步桥

- plan 有 Task 局部执行顺序与 schedule；
- 动态/生成路径均有 per-Task 入口；
- 跨 Task 音频必须经过 `async_bridge` 或明确合流点；
- SPSC Ring Buffer 统计水位、欠载和溢出。

### 3.4 有限运行

主动推进宿主按 WAV 总帧数、`duration_frames` 或默认时长决定停止；pacing 使用 `processed_frames / sample_rate` 与 monotonic wall clock 对齐。

## 4. 当前关键缺口

### 4.1 没有绝对样本位置

`OrpheusProcessContext.timestamp` 字段存在，但动态 Runtime 和生成代码都固定写入 `0.0`。组件无法知道当前块对应时间线的哪一段，也无法做确定性的时间戳、自动化或跨流对齐。

### 4.2 没有 epoch 和不连续语义

reset、seek、重新启动、文件切换、欠载补零目前没有统一的 timeline epoch / discontinuity 标志。组件无法区分连续音频和时间跳变。

### 4.3 Task 时间线仍近似全局单速率

compiler 当前从所有时钟源解析一个图速率，并写回所有 Task。Task 虽有独立 block/tick/counter，但尚未真正拥有独立速率、主时钟身份及与其它 Task 的有理数时间映射。

### 4.4 调度只有 period，没有 phase

所有周期节点默认从同一相位起跑，不支持明确的 `phase_frames`、启动偏移、deadline 或 release time。复杂流水线只能依赖拓扑顺序和隐式首块行为。

### 4.5 延迟字段没有形成系统能力

manifest 有 `latency_samples`，但当前内置组件均填 0，compiler 未汇总端到端路径延迟，也未在合流点插入补偿。算法延迟、重采样滤波延迟、桥缓冲延迟和设备延迟没有统一报告。

### 4.6 结束条件是宿主启发式

`duration_frames` 由若干 source 参数取最大值，WAV/MP3 由宿主扫描文件长度。没有 EOS 在图内传播，也没有“所有活动源结束/某 sink 完成”的正式停止协议。

### 4.7 独立时钟只缓冲，不校正

异步 Ring Buffer 可以吸收短期抖动并统计欠载/溢出，但没有：

- 时钟比率估计；
- PLL/水位控制器；
- 异步采样率转换（ASRC）；
- sample slip/drop 策略；
- 带时间戳的跨域帧。

长期运行两个独立晶振仍会水位漂移。

### 4.8 控制和观测只有块时间

控制链固定一块延迟，但没有目标 frame/块内 sample offset。Probe/Observation 数据也缺少统一的 epoch、frame index、sequence 和 dropped 元数据。

## 5. 目标时间线契约

### 5.1 Process Context

下一版 ABI 在末尾增加整数时间字段，保留 `timestamp` 兼容：

```c
typedef struct OrpheusTimelineContext {
    uint64_t frame_index;      /* 本块首帧在所属 timeline 的绝对位置 */
    uint64_t epoch;            /* reset/seek/restart 后递增 */
    uint32_t valid_frames;     /* 最后一块或欠载时的有效帧数 */
    uint32_t flags;            /* DISCONTINUITY / EOS / CONCEALED / DROPPED */
} OrpheusTimelineContext;
```

`timestamp` 由 `frame_index / sample_rate` 派生，不作为累积真值，避免长时间 double 精度和漂移问题。

### 5.2 Plan Timeline

建议新增：

```json
{
  "timeline": {
    "id": "main",
    "sample_rate_num": 48000,
    "sample_rate_den": 1,
    "tick_frames": 128,
    "trigger": "external|active"
  },
  "tasks": [{
    "timeline_id": "main",
    "phase_frames": 0,
    "period_frames": 128,
    "master": "device_in"
  }]
}
```

使用有理数速率，避免 44.1/48 kHz 或长期换算使用浮点。

### 5.3 Buffer 时间元数据

普通同域边可从 Task 上下文隐式继承时间，不必每个 Buffer 重复存储。以下边界必须显式携带：

- 跨 Task/跨时钟域桥；
- 外部输入与输出；
- 观测快照；
- 可 seek 文件源；
- 欠载补零或丢帧恢复。

桥帧头至少包含 `timeline_id / epoch / frame_index / frames / sequence / flags`。

## 6. 分阶段实施建议

### P0：建立绝对图时间

1. Runtime 与生成器按实际处理帧推进 `frame_index`，不再把 timestamp 固定为 0；
2. 每个 Task 保存自己的 frame counter，process context 传首帧位置；
3. reset/restart 增加 epoch，首次块带 DISCONTINUITY；
4. 动态/生成一致性测试验证每个节点收到相同 frame index；
5. plan 显式记录执行触发 `external|active`，UI 只在 active 下显示 pacing。

这是后续能力的基础，优先级最高。

### P1：正式结束语义与延迟模型

1. source 输出 EOS / valid_frames，宿主以图内结束条件停止，替代最大时长启发式；
2. `latency_samples` 支持常量和参数表达式；
3. compiler 计算 source-to-sink 累计延迟并生成报告；
4. 合流节点检查路径延迟，按策略报错或插入显式 delay compensation；
5. 文件 seek/reset 递增 epoch 并传播 DISCONTINUITY。

### P2：独立时钟域与漂移控制

1. 每个 Task/设备域明确 clock master，不再把单一采样率覆盖全部 Task；
2. async bridge 帧携带时间戳与 sequence；
3. 根据 Ring Buffer 水位估计时钟偏差 ppm；
4. 增加 ASRC/PLL 或可选 sample-slip 策略；
5. 报告端到端设备延迟、桥延迟和 drift 状态。

### P3：采样级事件和可观测性

1. 参数事件携带 `target_frame` 与块内 `sample_offset`；
2. control link 可选择 block boundary 或 sample-accurate 策略；
3. Observation envelope 统一携带 timeline/epoch/frame/sequence/dropped；
4. 支持确定性录制与重放：相同输入、事件时间线和 plan 产生一致输出；
5. UI 显示图时间、墙钟、延迟和不连续标记，不再只显示“运行中”。

## 7. 建议的概念层级

```text
执行实现
├── 动态 Runtime
└── 生成代码

执行触发
├── 外部节拍触发
│   ├── 声卡 callback
│   ├── DMA ISR
│   └── hardware timer
└── 主动推进
    ├── pacing=full_speed
    └── pacing=wall_clock

图时间线
├── timeline / clock domain
├── sample rate（有理数）
├── absolute frame index
├── epoch / discontinuity / EOS
├── phase / period
└── latency / drift

访问端点
├── local pipe/inproc
├── UART/USB/TCP
└── SHM/callback
```

图时间线是执行语义，不是 UI 模式；执行触发只决定何时推进它；访问端点只决定谁能控制和观察它。

## 8. 验收标准

- 任一组件在动态与生成路径收到相同的 frame index、epoch 和 flags；
- 全速与按墙钟运行产生相同帧数、时间戳和音频输出；
- 设备回调大小变化不改变图时间，只改变一次回调包含的 tick 数；
- seek/reset 后下游明确看到 discontinuity；
- 多 Task 可证明时间映射，不依赖全局 block counter 猜测；
- 两个独立设备长期运行时，Bridge 水位有界且可报告 drift ppm；
- 合流路径的累计延迟可查询、可验证、可补偿；
- Probe/Observation 数据可按 frame index 与音频结果精确对齐。
