# orpheus.builtin.sport_tdm_in — SPORT/TDM 输入

`sport_tdm_in` 是外部 SPORT/TDM DMA 数据进入 Orpheus 图的音频边界。组件本身不初始化 SPORT、DAI/SRU、PCG 或 DMA；它消费板级 SPORT callback 已经转换并放入内存的 slot 数据块，然后把逻辑通道的交错 `f32` 数据送入图。

## 适用层级

当前实现和验证目标是 `adsp21593`（ADSP-21593）。架构模式来自 ADI SHARC/SHARC+ 21xxx 的 SPORT/TDM/DMA/DAI 边界；具备同等能力的 SHARC+ 目标可以按同一模式扩展，但在 Orpheus 中尚不构成已支持目标。

## 端口

| 端口 | 方向 | 类型 | 格式 | 通道 |
| --- | --- | --- | --- | --- |
| `out` | 输出 | audio | `f32` | `channels` |

输出数据为交错布局，每个 Task 块包含 `frames * channels` 个 `float`。

## 参数

| 参数 | 类型 | 范围 | 默认值 | 更新策略 |
| --- | --- | --- | --- | --- |
| `channels` | int | 1–64 | 2 | restart_required |
| `sample_rate` | int | 1000–384000 | 48000 | restart_required |

`channels` 是图内的逻辑通道数，而不是 SPORT 硬件的 TDM slot 总数。例如 32 个 TDM slot 中只使用 slot 2 和 5 时，图内可以声明 `channels: 2`，由顶层 `sport_bindings` 把这两个 slot 映射到逻辑通道 0 和 1。

这两个参数刻意很少：通道数和采样率属于图端口签名；SPORT resource、slot 映射、slot 数、样本格式属于工程顶层 `sport_bindings`；时钟域、触发组、块长属于顶层 Task/调度契约。把它们留在编译期配置中，可以静态展开 gather 和格式转换，避免实时路径里的动态映射表和配置分支。

## 状态

| 字段 | 说明 |
| --- | --- |
| `channels` / `sample_rate` | prepare 时写入的图签名 |
| `src` | adapter 填充的交错 `f32` 输入块指针 |
| `src_frames` | adapter 实际填充的帧数 |
| `underruns` | adapter 提供的数据帧数不足时的累计次数 |

## 处理行为

`process()` 最多把 `src_frames` 帧拷贝到输出端口；不足的帧补零。任何一次帧数不足都会递增 `underruns`。组件不等待硬件，也不阻塞实时线程。

## 生成代码关系

代码生成器会为节点生成：

```c
extern float g_sport_in_<node>[];
SportTdmInState* orpheus_sport_tdm_in_state_<node>(void);
```

`orpheus_sport.c` 中生成的 SPORT wrapper 先把 DMA RX slot gather、定点转 `f32`，写入 `g_sport_in_<node>`，再设置 `src = g_sport_in_<node>`、`src_frames = trigger->frames`，随后调用图 Task。

## DSP 集成方法

1. 在工程中声明 `target: adsp21593`、时钟域、触发组、Task、`sport_bindings`，并把该组件放入对应 Task。
2. 使用 `orpheus_core.cli generate ... --target adsp21593` 生成独立 C 工程。
3. 把生成工程源码和组件源码加入 CCES 工程，保持 C11 编译选项。
4. 板级代码初始化 DAI/SRU routing、PCG/codec 时钟、SPORT 协议、TDM slot 宽度、DMA descriptor 和中断。
5. 在 SPORT callback 收齐本 Task 所需 RX buffer 后，构造 `OrpheusSportIo_<task>` 与 `OrpheusExternalTriggerContext`，调用 `orpheus_graph_process_task_<task>_sport()`。
6. 通过 trigger 统计和组件 `underruns` 检查块长、相位与数据供应是否正确。

板级 DMA buffer 的 cache 一致性、对齐和 invalidate/flush 由 CCES 工程负责。重启或失锁恢复后应递增 `epoch`，并设置 `ORPHEUS_TIMELINE_DISCONTINUITY`。

## 实时安全与限制

- `process()` 只执行拷贝和补零，没有动态分配、锁、文件/网络 IO 或日志。
- 参数会改变图签名，必须重新编译/重启，不能实时热改。
- `src == NULL` 或 `src_frames == 0` 时输出静音并递增 `underruns`。
- 组件不配置或校准硬件锁相；`user_guaranteed` 时钟域仍由板级工程保证。
