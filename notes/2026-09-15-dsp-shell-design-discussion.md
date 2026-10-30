# DSP 壳子集成与边界模型讨论草稿

- 日期：2026-09-15
- 状态：非正式讨论草稿
- 背景：评估 Orpheus 生成代码集成到 ADSP 生产 DSP 壳子的可行性，并讨论 I/O 格式、TDM slot 映射、时钟与内存/实时预算的建模方式。
- 说明：本文只记录讨论和候选设计，不是正式架构文档或已实现特性说明。

## 结论摘要

1. Orpheus 音频端口已有 `sample_format` 字段，但当前生成器与 runtime 实际只稳定支持 f32。
2. 短期图内部继续统一 f32；先让 `embed_in/embed_out` 支持外部传输格式。
3. TDM slot 映射应与逻辑通道重排分开；建议引入 SPORT/TDM 边界组件。
4. 多 SPORT 回调不应强制建模为“必须从一个 clock source 分出”，应允许多个 trigger 绑定同一个 clock domain。
5. 静态内存估计可以先做目标无关报告；栈水位和 MIPS 先做实测模板。
6. SHARC linker 先生成建议，不第一版生成完整 LDF。

## 音频格式现状

ABI 中的音频端口已声明：

```c
typedef enum {
    ORPHEUS_FORMAT_F32 = 0,
    ORPHEUS_FORMAT_I16 = 1,
    ORPHEUS_FORMAT_I24 = 2,
    ORPHEUS_FORMAT_I32 = 3
} OrpheusSampleFormat;
```

`OrpheusPort` 里已有 `sample_format` 字段。编译器也会检查音频连接两端的 `sample_format` 是否一致。

但当前生成器和 runtime 的缓冲区、拷贝、staging、discard 逻辑基本都按 `float` 处理，并固定写入 `ORPHEUS_FORMAT_F32`。因此更准确的说法是：格式字段存在，但执行链路只落地了 f32。

控制参数已有 `OrpheusValue`，支持 `float/int/bool/string/bulk_ref`。控制连接校验要求源和目标类型严格一致，并检查 shape，不允许隐式转换。

## 定点输入/输出声明

建议不要立即让所有组件支持定点处理，而是区分两层：

```text
处理格式：图内部统一 f32
传输格式：embed_in / embed_out 的外部硬件格式
```

可以在 `embed_in` / `embed_out` 上增加：

```yaml
external_format: s24_left_in_s32
scale: 1/2^31
saturation: clip
```

常见格式建议：

- `f32`
- `i16`
- `s24_right_in_s32`
- `s24_left_in_s32`
- `q1_31`

“外面转好再进来”完全可行，而且是风险最低的第一阶段方案：

```text
DMA int32 buffer
  -> DSP 壳子手工转换成 f32
  -> Orpheus f32 graph
  -> DSP 壳子手工转换回 int32
  -> DMA TX buffer
```

优点是不改 ABI、不改 runtime、不改现有组件，能尽快验证真实 DSP。缺点是格式和缩放知识散落在壳子里，编译器无法检查格式错误，转换开销也不在 Orpheus 的内存/实时预算中。

如果做转换组件，类型必须尽量在编译期确定。生成器应只实例化实际使用的转换函数，例如 `convert_s24_left_in_s32_to_f32()` 和 `convert_f32_to_s24_left_in_s32()`，避免运行期任意选择类型导致无用分支进入 DSP 目标。

## TDM slot 映射

Orpheus 图里的“通道”是逻辑通道；SPORT/TDM 缓冲区中的“slot”是物理时隙。两者不是天然相同。

生产示例：

```text
SPORT 6A: 16 slots/frame
有效 slot: 0,1,2,4,5,7,8,9,10,11,12,13,14,15
无效 slot: 3,6

SPORT 6B: 16 slots/frame
有效 slot: 0..7

合计逻辑输入: 22
```

如果只声明 `channels: 22`，Orpheus 不知道逻辑通道 0 来自 6A slot 0、逻辑通道 3 来自 6A slot 4、逻辑通道 14 来自 6B slot 0，也不知道输出 7A 的每个 slot 应该写入哪个逻辑通道。

选择 `target: dsp` 可以排除 Windows 组件，但这只回答“平台是否能用某个组件”。slot 映射回答的是“同一块板子上，硬件 buffer 布局是什么”。不同 DSP 板子可能有不同 SPORT number、slot 数、有效 slot、通道顺序、定点格式和 RX/TX 资源，所以 slot 映射不等同于平台字段。

候选方案：

1. 扩展 `embed_in/embed_out`：让现有边界组件携带 `physical_map`。改动小，但通用边界组件会和硬件接口细节耦合。
2. 新增 `sport_tdm_in/out`：职责清晰，SPORT 特有语义集中，便于静态 gather/scatter 和物理映射检查。
3. 全部物理通道进入图，再用 router 挑选：原型简单，但会出现无效通道、更大 buffer，且板级知识泄漏到 graph。

推荐方向是新增 `sport_tdm_in/out`：

```text
SPORT 6A/6B DMA buffers
  -> sport_tdm_in 22ch
  -> gain / router / business graph
  -> sport_tdm_out 16ch
  -> SPORT 7A/7B DMA buffers
```

`sport_tdm_in/out` 负责物理 slot、格式转换和硬件边界；`channel_router` 只负责逻辑通道重排；业务组件只看逻辑通道。slot 映射应是编译期参数，并影响端口签名。生成器可以静态展开：

```c
logical[0] = convert(dma[slot0]);
logical[1] = convert(dma[slot1]);
logical[2] = convert(dma[slot2]);
logical[3] = convert(dma[slot4]);
```

无效 slot 根本不进入逻辑图。

## 时钟与多个 SPORT 回调

从软件入口看，多个 SPORT callback 像多个独立时钟：

```text
SPORT 6A RX callback
SPORT 6B RX callback
SPORT 2B RX callback
SPORT 0A TX callback
```

但硬件上它们可能由同一个 PCG、codec frame sync 或外部 BCLK/Fsync 驱动。因此不能简单说“每个 callback 就是一个独立 clock source”，也不能强制所有 task 必须从一个图内 clock source 数据流节点派生。

建议拆分概念：

| 概念 | 含义 |
|---|---|
| clock domain | 一条硬件采样时间轴 |
| hardware clock | PCG、codec、外部 BCLK/Fsync 等物理来源 |
| trigger endpoint | 某个 SPORT RX/TX DMA callback |
| task | 由 trigger 驱动的执行单元 |
| phase / offset | trigger 或 task 在时间轴上的相位 |
| sync constraint | 多个 trigger/task 之间的同步关系 |
| data transfer | 跨 task 数据移动方式 |

核心原则：

1. clock domain 是硬件事实，不是图拓扑推导结果。
2. 一个 clock domain 可以有多个 trigger endpoint。
3. 多个 task 可以绑定同一个 clock domain。
4. 不同 task 的 block size 可以不同。
5. 跨 task 不能默认共享 buffer。
6. 如果没有硬件同步证据，应默认视为不同步。
7. 同步关系应由板级 profile 显式声明。

示例模型：

```yaml
clock_domains:
  - id: audio48
    rate: 48000
    hardware_clock: pcg_a
    assurance: user_guaranteed

triggers:
  - id: sport6a_rx
    clock_domain: audio48
    period: 32

  - id: sport2b_rx
    clock_domain: audio48
    period: 8
    accumulate: 3

tasks:
  - id: entertainment
    clock_domain: audio48
    trigger: sport6a_rx
    block_size: 32

  - id: rnc_amp
    clock_domain: audio48
    trigger: sport2b_rx
    block_size: 24
```

同一 domain 内的常见情况：

1. 同 block、同 phase：例如 6A RX 和 6B RX 属于同一次 block，可由一个 master callback 驱动，一个 task 消费两个 SPORT buffer。
2. 同域不同 block size：例如 entertainment 32 帧、RNC/AMP 24 帧。两个 task 有独立入口，但共享同一绝对样本时间轴；跨 task 数据必须走 queue、double buffer、SPSC ring、rate bridge 或 async bridge。
3. 同域不同 callback：多个 callback 可以触发不同 task，但传入的 `frame_index` 必须属于同一绝对样本坐标。

如果两个物理时钟只是标称速率相同，但来自不同晶振，应声明不同 clock domain。例如 A2B audio 48.000 kHz 和 local codec 48.000 kHz 没有锁相，就应该建模为两个 domain，跨域传输使用 async bridge、水位控制、sample-slip 或 ASRC。

运行期可选方式：

1. 一个主时钟回调统一派发所有 due task；
2. 每个 SPORT callback 触发自己的 task；
3. callback 只写 ring buffer，低优先级线程/循环消费；
4. 高优先级音频直接在 ISR 内执行，低优先级任务延迟处理。

选择方式取决于 ISR 执行时间、截止时间、任务优先级、共享数据关系、是否需要 barrier，以及是否允许跨任务延迟。Orpheus 第一阶段不必自动选择最优调度，但应允许显式描述。

硬件同步不等于软件执行同步。ISR 优先级、处理时间和 DMA 完成时间都会造成执行相位差。

## 内存与 MIPS 估计

### 内存

静态内存估计是可行且优先级高的能力。生成器已经知道大部分关键信息：

| 项目 | 来源 |
|---|---|
| 组件状态 | `memory.state_size` |
| 组件 scratch | `memory.scratch_size` |
| 图中间 buffer | plan buffer 分配 |
| staging/discard buffer | plan 或 generator |
| I/O buffer | `embed_in/embed_out` 或 SPORT 边界 |
| bridge/ring buffer | bridge 配置 |
| 控制状态 | control links |

可以先输出目标无关报告：

```text
component_states     : xx KB
graph_buffers        : xx KB
stage_buffers        : xx KB
io_buffers           : xx KB
bridge_queues        : xx KB
runtime_misc         : xx KB
static_total         : xx KB
```

再按组件和节点列明细。

需要注意：

- 这个报告不是最终 SHARC linker map；
- 不包含 linker 对齐 padding 的完整精确结果；
- 不包含编译器临时生命周期折叠后的最优值；
- 不能替代真实 map 文件；
- 但足够早期判断是否会明显超过 L1/L2。

### 栈

栈是最难静态估计的部分。短期建议做运行时栈水位：

1. 初始化时把栈区域填充固定 pattern；
2. 运行一段时间；
3. 从末端扫描第一个被改写的位置；
4. 输出 high-water mark。

这个方法目标无关，只需要知道栈区域范围。

### MIPS/cycle

精确 WCET 很难，尤其在有 cache、分支、库函数和硬件差异的情况下。短期建议做实测模板：

```c
cycle_start = orpheus_cycle_counter();
orpheus_graph_process(...);
cycle_end = orpheus_cycle_counter();
```

记录：

- min cycles；
- avg cycles；
- max cycles；
- overrun count；
- callback interval；
- jitter；
- queue watermark；
- underrun/overrun。

组件 manifest 后续可以加可选粗略成本模型：

```yaml
cost_model:
  cycles_base: 120
  cycles_per_frame: 3
  cycles_per_channel_frame: 2
```

但这只能作为规划估计，不能替代实测。

## SHARC linker 建议

完整生成 SHARC LDF 的复杂度较高，尤其在缺少厂商工具链和板级内存配置时。

短期建议只生成建议：

| 内容 | 建议 |
|---|---|
| 热路径代码 | fast code section |
| 组件状态 | L1/L2 data |
| 大 graph buffer | L2 |
| DMA buffer | DMA 可访问区域 |
| bridge buffer | 独立 section |
| 常量表 | 常量 section |

Orpheus 可以输出：

```text
buffer: g_graph_buf_0
size: 4096 B
access: DMA + core
suggested_section: seg_l2_data
reason: large, not hot scratch
```

最终 LDF 由 CCES 工程或板级工程确认。

## 候选落地顺序

1. 当前 DSP 壳子继续使用手工 int32/f32 转换，先跑通真实音频。
2. 给 `embed_in/embed_out` 增加 `external_format`，生成边界转换。
3. 引入 `sport_tdm_in/out` 边界组件，承载 SPORT/TDM slot 映射。
4. 允许多个 trigger 绑定同一个 clock domain。
5. 生成目标无关静态内存报告。
6. 增加 cycle / jitter / queue 水位 / 栈水位实测模板。
7. 可选生成 SHARC linker section 建议，不第一版生成完整 LDF。

## 待确认问题

1. `external_format` 放在节点参数、端口字段还是组件 manifest？
2. `sport_tdm_in/out` 是通用边界组件，还是 ADI SPORT 专用组件？
3. slot 映射影响签名时，是否需要独立的 board profile 文件？
4. clock domain 的 `assurance` 如何限制部署？
5. 多 trigger group 的 barrier 语义是否必须在 V1 支持？
6. 内存报告采用 JSON、Markdown，还是两者都生成？
7. runtime profiling hook 是否要编译开关控制，避免产品目标携带额外开销？
