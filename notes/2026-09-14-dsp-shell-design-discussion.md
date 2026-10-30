# DSP 壳子集成设计讨论草稿

- 日期：2026-09-14
- 状态：讨论草稿，未纳入正式文档
- 背景：评估 Orpheus 生成代码集成到 ADSP-21569 生产壳子的可行性。

## 结论要点

1. Orpheus 音频端口目前已有 `sample_format` 字段，但生成器和 runtime 实际只稳定支持 f32。
2. 短期图内部继续统一 f32；先让 `embed_in/embed_out` 支持外部传输格式。
3. TDM slot 映射应与逻辑通道重排分开；建议后续引入 SPORT/TDM 边界组件。
4. 多 SPORT 回调不应强制建模为“必须从一个 clock source 分出”，应允许多个 trigger 绑定同一个 clock domain。
5. 内存估计可以先做目标无关的静态报告；栈水位和 MIPS 先做实测模板。

## 音频格式

### 现状

ABI 中的音频端口已声明：

```c
typedef enum {
    ORPHEUS_FORMAT_F32 = 0,
    ORPHEUS_FORMAT_I16 = 1,
    ORPHEUS_FORMAT_I24 = 2,
    ORPHEUS_FORMAT_I32 = 3
} OrpheusSampleFormat;
```

`OrpheusPort` 里已有 `sample_format`。编译器也会检查音频连接两端的 `sample_format` 是否一致。

但当前生成器和 runtime 的缓冲区、拷贝、staging、discard 逻辑基本都按 `float` 处理，并固定写入 `ORPHEUS_FORMAT_F32`。因此更准确的说法是：格式字段存在，但执行链路只落地了 f32。

控制参数已有 `OrpheusValue`，支持 `float/int/bool/string/bulk_ref`。控制连接校验要求源和目标类型严格一致，并检查 shape，不允许隐式转换。

### 建议分层

短期不要让所有组件立刻支持定点，而是区分两层：

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

### 转换方案取舍

| 方案 | 优点 | 缺点 |
|---|---|---|
| 壳子手工转换 | 最快，当前已验证 | 格式知识留在壳子，无法编译期检查 |
| I/O 边界格式声明 | 生成边界转换，可检查、可统计开销 | 需要扩展 generator / runtime |
| 通用转换组件 | 可表达图内混合格式 | 若运行期选择类型，无用分支难裁剪 |

推荐顺序：

1. 当前壳子手工转换。
2. `embed_in/embed_out` 增加 `external_format`，由生成器生成边界转换。
3. 如图内部需要混合格式，再引入显式 `convert` 组件。

如果做转换组件，类型必须在编译期确定，生成器只实例化实际使用的转换函数，例如：

```text
convert_s24_left_in_s32_to_f32
convert_f32_to_s24_left_in_s32
```

避免运行期任意选择类型导致无用代码进入 DSP 目标。

## TDM slot 映射

选择 `target: dsp` 可以通过平台过滤排除 Windows 组件，但这只解决“平台是否可用”，不解决“逻辑通道来自哪个物理 slot”。

生产 6A 路径示例：

- TDM 每帧 16 slot；
- 有效输入 slot：`0,1,2,4,5,7,8,9,10,11,12,13,14,15`；
- slot 3 和 slot 6 无效；
- 6B 使用 slot `0..7`；
- 合计 22 个逻辑输入通道。

更完整的意图示例：

```yaml
embed_in:
  channels: 22
  external_format: s24_left_in_s32
  physical_map:
    - logical: 0..13
      sport: 6A
      slots: [0,1,2,4,5,7,8,9,10,11,12,13,14,15]
    - logical: 14..21
      sport: 6B
      slots: [0..7]

embed_out:
  channels: 16
  external_format: s24_left_in_s32
  physical_map:
    - logical: 0..15
      sport: 7A
      slots: [0..15]
```

职责建议拆分：

```text
SPORT 6A/6B DMA buffers
        ↓
sport_tdm_in 22ch
        ↓
gain / router / business graph
        ↓
sport_tdm_out 16ch
        ↓
SPORT 7A/7B DMA buffers
```

`sport_tdm_in` / `sport_tdm_out` 只负责物理边界、格式转换和 slot 映射；`channel_router` 只做逻辑通道重排。

slot 映射应是编译期参数，并且影响端口签名。生成器可以静态展开为：

```c
out[0] = convert(in[slot0]);
out[1] = convert(in[slot1]);
out[2] = convert(in[slot2]);
out[3] = convert(in[slot4]);
```

这比“把所有物理通道接进来再用 router 挑”更干净，也能减少无效 buffer 和错误连接。如果先用 `embed_in` 收满全部 slot，再交给 `channel_router`，可以作为过渡方案，但边界契约不够清晰。

## 时钟与多个 SPORT 回调

### 关键问题

从代码入口看，多个 SPORT callback 像多个独立时钟；硬件上可能又由同一个 PCG 或 codec frame sync 驱动。这个关系不能靠运行时猜测，也不能完全从图数据流推导。

应拆分几个概念：

| 概念 | 含义 |
|---|---|
| clock domain | 硬件采样时间轴，例如 48 kHz audio |
| trigger endpoint | SPORT RX/TX callback 或 DMA buffer complete |
| task | 某个 block size、某个 trigger 驱动的执行单元 |
| sync constraint | 两个 trigger/task 之间的硬件同步关系 |
| data transfer | 跨 task 的显式队列 / bridge |

### 不建议的模型

不要强制所有 task 必须从同一个图内 clock source 数据流节点派生。clock domain 是硬件事实，不是图拓扑的必然结论。

### 建议模型

允许多个 task 绑定同一个 clock domain：

```yaml
clock_domains:
  - id: audio48
    rate: 48000
    hardware_clock: pcg_a

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
    trigger: sport6a_rx
    block_size: 32

  - id: rnc_amp
    trigger: sport2b_rx
    block_size: 24
```

编译器需要检查：

- task 声明的 clock domain 是否存在；
- 同一 domain 内采样率/周期是否可推导；
- 跨 domain 连接是否走 bridge；
- 同 domain 不同 task 的数据传递是否显式经过队列或同步 bridge。

如果硬件确实同步，就声明同一个 clock domain；如果没有证据，默认视为不同步。

运行期适配方式：

1. 一个主时钟回调统一派发；
2. 每个 SPORT callback 触发自己的 task；
3. 同域跨速率数据通过静态合流点、rate bridge 或 async bridge。

硬件同步不等于软件执行同步。DMA 回调时间、ISR 优先级和执行时间抖动仍会造成相位差，因此共享数据必须显式同步。

## 内存与 MIPS

### 内存

静态内存估计可以优先做，且目标无关。生成器已经知道：

- 组件 `state_size`；
- 组件 `scratch_size`；
- 图中间 buffer；
- staging / discard buffer；
- `embed_in/embed_out` 缓冲区；
- bridge/ring buffer；
- 控制状态。

示例报告：

```text
component_states     : xx KB
graph_buffers        : xx KB
stage_buffers        : xx KB
io_buffers           : xx KB
bridge_queues        : xx KB
runtime_misc         : xx KB
static_total         : xx KB
```

这个报告不等于最终 SHARC linker map，但适合早期判断是否明显超过 L1/L2 容量。

栈可以先做栈水位测量：初始化时填充固定 pattern，运行后扫描最高使用点。MIPS/cycle 先做实测框架，不要先造复杂静态模型。

### 实时预算

建议模板记录：

- task 处理前后 cycle count；
- min / avg / max cycles；
- overrun 次数；
- callback 间隔和 jitter；
- queue 水位；
- underrun / overrun；
- stack high-water mark。

组件 manifest 后续可加可选成本模型，例如 `cycles_per_frame`、`cycles_per_channel_frame`，但只作为规划值，不替代实测。

### SHARC linker 建议

第一版不生成完整 LDF。先生成建议和 section 命名，例如：

- 热路径代码建议放 fast code section；
- 组件状态建议放 L1/L2 data；
- 大 graph buffer 建议放 L2；
- DMA buffer 放 DMA 可访问区域；
- bridge buffer 独立 section。

最终 LDF 仍由 CCES 工程或板级工程确认。

## 落地顺序

1. 壳子手工 int32/f32 转换，先把真实 DSP 跑通。
2. `embed_in/embed_out` 增加 `external_format`，生成边界转换。
3. 引入 `sport_tdm_in/out` 边界组件，描述物理 slot 映射。
4. 支持多个 trigger 绑定同一 clock domain。
5. 生成目标无关静态内存报告。
6. 增加 cycle / jitter / queue 水位 / 栈水位实测模板。
7. 可选生成 SHARC linker section 建议，不第一版生成完整 LDF。
