# EREV BAF 结构参考说明

对应工程：`examples/recycled/symphony_baf_structural_reference.yaml`

> 重要：本工程不是完整算法蒸馏，也不具备与 BAF 生成代码逐样本或听感等价的资格。它是结构参考，用于保存拓扑、通道尺寸、任务周期、TOP 分区和 BVP 边界。

## 1. 蒸馏范围

本工程以 `cart-cicd-erev/components/baf/src` 的 2026-10-08 构建输出为准，覆盖两个执行图：

- Baf1 / `Model_1_1`：30 路主输入、22 路 Preq 输入、22 路主输出、32 路 Preq 输出，以及 6 个 TID。
- Baf2 / `Model_1_2`：Medusa 降速率空间处理、Headrest EQ、Overhead EQ，以及 5 个 TID。
- `BVP_Config.yml`：Baf1 到 Baf2 的 488 元素总线，以及 Baf2 到 Baf1 的 1193 元素总线。

ASM 的 EHC/RNC 属于另一模型，继续保留在 `symphony_asm_ehc_rnc.yaml`，不并入本工程。

## 2. 源代码实证

### Baf1 调度

`Baf1.exec_graph.json` 与 `Model_1_1_step0..5` 给出：

| TID | 周期 | 频率 | 责任 |
|---|---:|---:|---|
| 0 | 0.6667 ms | 1500 Hz | FDP 全速率 IO、Part3-6、InputSelect、PreAmp、PostProcess、Audiopilot |
| 1 | 1.3333 ms | 750 Hz | Audiopilot ref/mic 缓冲 |
| 2 | 2.6667 ms | 375 Hz | FDP 256 点 STFT 核心 |
| 3 | 42.6667 ms | 23.4375 Hz | Audiopilot HF FFT/相干估计 |
| 4 | 170.6667 ms | 5.8594 Hz | LF 速度界限与 NoiseSlew |
| 5 | 512 ms | 1.9531 Hz | 相干累积与慢速控制 |

### Baf2 调度

`Baf2.exec_graph.json` 与 `Model_1_2_step0..4` 给出：

| TID | callrate | 频率 | 责任 |
|---|---:|---:|---|
| 0 | 1 | 4500 Hz | 仅推进 rate-monotonic scheduler |
| 1 | 3 | 1500 Hz | Part1 Bands、Part3 MixingControl、Part5 Deci PostHoligram |
| 2 | 12 | 375 Hz | Part1 Bands 慢速状态 |
| 3 | 13 | 346.1538 Hz | Deci FDP、Mixing、Peripheral/Headrest/Overhead EQ、PostHoligram |
| 4 | 52 | 86.5385 Hz | Deci FDP 慢速核心 |

TID3 的 13 分频不是 48 kHz 音频块的整数倍，因此当前 Orpheus 图把 Baf2 作为一个复合处理域呈现，并在 `model_tree.model_1_2_task_flows` 中保留真实调度，不伪造整数块任务。

## 3. 结构信号流

### Baf1 / Model_1_1

```text
MusicIn 30ch + FullRate 回授
  -> InputSelect 30->12
  -> PreAmp / InputMixer / Tone / Balance / Volume
  -> PostProcess 32->22
  -> 主输出

Treble LR 2ch
  -> FDP 2->6
  -> Part3 Mixing 6+7->22
  -> Peripheral EQ
  -> PostHoligram / Summation / SpeakerDelay / SpatialFader
  -> 反馈到 MusicIn

PreAmp Buffer1/Buffer2 + FDP 抽头 + Mono
  -> Audiopilot35
  -> 10ch 自适应输出
```

### Baf2 / Model_1_2

```text
Baf1 Audiopilot/当前增益 488 元素总线
  -> MedusaPart1Bands
  -> MedusaPart2FdpDeciRate
  -> MedusaPart3MixingControl + DeciRateMixing
  -> Deci Peripheral EQ
  -> Headrest EQ / Overhead EQ
  -> MedusaPart5DeciRatePostHoligram
  -> Mono / Treble LR / Treble Surround / 22ch PostHoligram + 控制量
```

## 4. 双向 BVP 总线

### Model_1_1 -> Model_1_2：488 元素

| 字段 | 数量 | Orpheus 投影 |
|---|---:|---|
| Audiopilot35_Out1 | 320 | `full_rate_bus`，10ch x 32 |
| CsCurrentGain | 26 | 模型树记录精确形状 |
| LeftCurrentGain | 70 | 模型树记录精确形状 |
| RightCurrentGain | 70 | 模型树记录精确形状 |
| FadeGains | 2 | 模型树记录精确形状 |

### Model_1_2 -> Model_1_1：1193 元素

| 字段 | 数量 | Orpheus 投影 |
|---|---:|---|
| CaeEnable / DecayRate | 1 + 1 | 标量控制投影 |
| Mono | 32 | 1ch x 32 端口 |
| TrebleLr | 64 | 2ch x 32 端口 |
| TrebleSurround | 224 | 7ch x 32 端口 |
| Cs/Left/Right TargetGains | 26 + 70 + 70 | 模型树记录精确形状 |
| PostHoligram_AudioOut | 704 | 22ch x 32 端口 |
| FastSequence | 1 | 模型树记录标量语义 |

当前运行时执行 float/int/bool/string 标量控制链；数组控制只做编译期形状校验。因此 26/70 元素矩阵保持在 `cross_model_buses` 中，音频块使用显式端口，避免把数组伪装成可执行标量。

## 5. 控制链状态

当前没有把 BVP 控制伪装成可执行标量连接。真实控制面至少包含：

- Baf1 -> Baf2：26/70/70 当前增益矩阵与 2 元素 FadeGains；
- Baf2 -> Baf1：CaeEnable、DecayRate、26/70/70 TargetGains 与 FastSequence；
- Part3 的序列去重、冻结/释放和一阶斜坡状态；
- RTC/TOP 分区的 reset、disable、shutdown 与系数事务更新。

Orpheus 现有标量控制链不能表达这些数组和状态机。YAML 只在 `cross_model_buses` 中保留字段数量，不执行人工近似闭环。

## 6. 忠实度边界与缺口

- BAF 分段 SoftClipper 使用专用 `baf_soft_clipper`，公式和默认参数与生成代码一致。
- 路由尺寸、延迟容量、IIR 级数、Part3 的 26/70/70 增益矩阵尺寸、BVP 字段长度与生成代码一致。
- GLXP poolIIR/FIR 目前只表达级数和通道轮廓，没有完整复现多滤波器映射与运行期更新。
- FDP 的完整 STFT/瞬态检测/混响提取仍以可组合结构和参数契约表达，当前实时图中的 2->6 `matrix_mul` 是软件投影，不宣称与专有生成代码逐样本一致。
- Baf2 的 Headrest/Overhead FIR 当前以单位脉冲表示直通结构；真实 TOP 分区和 tap 规模记录在 `model_tree.parameter_partitions`。
- Baf2 Part5 缺 VLS Row1/Row2 生成 FIR、16 路串扰消除 IIR 和 SleepingBeauty 多目标 ramper。
- Model_1_2 TID3 的 13 分频已准确记录为 346.1538 Hz，但未在当前单 Task plan 中执行。

完整算法蒸馏至少还需要：FDP STFT 组件、多滤波器 FIR bank、VLS 行生成/串扰消除、数组控制链、Medusa ramp sequencer、真实多 Task 调度，以及与生成代码的 golden 对比。

## 7. 验证

```powershell
python -m orpheus_core.cli validate examples/recycled/symphony_baf_structural_reference.yaml
python -m pytest orpheus_core/tests/test_baf_model_alignment.py `
  orpheus_core/tests/test_distill_import.py `
  orpheus_core/tests/test_symphony_step0.py -q
```

验收事实：

- schema/组件/端口/控制连接验证通过，无 warning；
- 展开后 114 个原子节点，其中 Baf2 16 个；
- 123 个画布/子图节点均有工程节点笔记；
- 不存在伪造的 BVP 标量控制连接；
- 主输出、Audiopilot 输出与 Baf2 四类输出端口均进入执行图；
- 动态运行端到端测试覆盖主输出和 Audiopilot 输出能量。