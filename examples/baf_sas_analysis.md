# BAF SAS 空间音频系统 - 蒸馏分析说明

> 源码：`Model_1_1.c` v7.736（905KB / 20642 行静态 C，Simulink Coder R2022b，ert_shrlib 目标）
> 目标 DSP：ADI SHARC+ GLXP | 基础块率：1500Hz（48kHz / 32 样本块）
> 可导入工程：`examples/baf_sas_full.yaml`（含 `model_tree`）

## 1. 系统概述

BAF（音频框架）SAS 空间音频系统，用于车载多通道音频处理。由 Talaria + Simulink Coder 生成静态 C 代码，经 BAF 框架（`Audio_Graph.c`）调度，`rtmodel.c` 按 TID switch 分派到 `Model_1_1_step0~5`。

两个 BAF 实例：
- **Baf1 / Model_1_1**：48kHz 实时音频主路径（本蒸馏对象）
- **Baf2 / Model_1_2**：1500Hz 降采样域控制/DSP（通过 IPC ping-pong 缓冲与 Baf1 交换数据）

参数调谐桥：`Model_InterpretationEngine64`（x86 PC 端 TSP 解释引擎）将运行期可调参数（TSP）解释为 DSP 状态变量（StateVar）。

## 2. Task 流程（6 个 TID）

| TID | 频率 | 周期 | 分频比 | 职责 |
|-----|------|------|--------|------|
| 0 | 1500Hz | 0.67ms | 1 | 主音频链：InputSelect → PreAmpPart1 → SAS Part2-6 → PostProcess → Audiopilot 调制 |
| 1 | 750Hz | 1.33ms | 2 | Audiopilot ref/mic 延迟缓冲（BufferRef 128、delayBuffer 226、BufferMic 128、Buffer 512） |
| 2 | 375Hz | 2.67ms | 4 | FDP 频域处理（256 点 STFT，2ch 入 6ch 出） |
| 3 | 23.4Hz | 42.7ms | 64 | Audiopilot FFT 噪声估计（256 点 RFFT 10ch + 窗 + 相干矩阵） |
| 4 | 5.86Hz | 170.7ms | 256 | Audiopilot LF 噪声速度界限（128 点速度轴 + noise slew） |
| 5 | 1.95Hz | 512ms | 768 | Audiopilot 30ch 相干求和（Gxx/Gyy/Gyx、magnitude²、saturation） |

## 3. 音频处理链（9 条，完整滤波器编排）

### 3.1 InputSelect（输入路由）
- `routerOutMap[30]`（tune 默认）或 `REQ_routerMap[30]`（RTC 覆盖）→ Variable Selector（sdspperm2，30ch→32ch）→ 主路径 + preq 路径
- 参数：routerOutMap（30 int）

### 3.2 PreAmpPart1（预放）
- MakeupGain → InputMixer3D（AddWeights: LFE+10dB / Lrs2Ls / Rrs2Rs）→ DownmixToStereo（Weights_L_R[8]）→ Send514Out → Bass/Midrange/Treble BoostCut → Balance → Volume → LevelDetect（PreEmpFilter pooliir 10stages×5coeffs）
- 参数（p0_b0）：
  - MakeupGainMakeupGain（1 float，默认 1.0）
  - InputMixer3dWeights_514（3 float）、InputMixer3dWeights_L_R（8 float）
  - LevelDetectPreEmpFilterpooliirCoeffs（50 float = 10stages×5）、NumStages（10 uint）
  - Bass BoostCoeffs[3] / CutCoeffs[3] / Maximum_dB
  - Midrange BoostCoeffs[5] / CutCoeffs[5] / Maximum_dB
  - Treble BoostCoeffs[3] / CutCoeffs[3] / Maximum_dB
  - Balance（TableDb[30] + TableIdx[30] + ChanRampMap[10] + RampTime + Offset = 75 值）
  - Volume（Table_dB[8] + Table_Idx[8] + RampTime + maxgain = 18 值）
  - LevelDetect（FastDecay / HoldMargin / HoldTime / MaxVol / MinVol / SlowDecay ×2 = 12 float）

### 3.3 Part2 FDP（频域环绕解码器）
- TrebleDelay[7928]（2ch）→ BufferIn（2ch×256）→ Windowing（InputOverlap×sine[128] + AudioIn×cosine[128]）→ RFFT（256 点，2ch→129 复数 bin×2）→ Coeffs1stStage（Lok/Rok/Lxk/Rxk = min(|L|,|R|)/|L| 等，SPS=|Lx-Rx|/(|Lx|+|Rx|)）→ LPF 平滑（lsGain 0.90993, lsPole 0.04504）→ Coeffs2ndStage（Lxks=1-Loks, Rxks=1-Roks）→ ApplyCoefficients（Lo=Lok×Lin, Ro=Rok×Rin, Lsr=Lxk×Lin, Rsr=Rxk×Rin → 6 路频域）→ PSD 平滑 → DetectImpulse → ReverbExtraction（4 条 fast/slow 延迟[2193]）→ IFFT+overlapAdd → BufferOut（6ch）→ Selector（4ch: {0,1,2,4}）
- 延迟：TrebleDelay（2ch, 7928, stateLen 3964）、SASFdpFullRate{L,R}{Fast,Slow}Delay（各 2193, stateLen 129, delay 1161）
- FFT：256 点，129 bin，2ch 入 6ch 出，sine+cosine 窗 128+128，overlap-add
- 参数（p3_b0）：FdpCoeffs（6 float）、FdpSpum（4 float）、DirectPathSamplesDec（1 uint, 1161）、TrebleDelay（1 uint, 1280）

### 3.4 Part3 全速率混合
- TrebleSurroundDelay[30436]（7ch, stateLen 4348, delay 1664）→ SelectSurroundDiscrete / SelectLeftSurroundAtmos / SelectRightSurroundAtmos → SumOfElements（LtfLtb+surround）→ 路由（b={0,2,3} c={1,4,5} d={7,9,10} e={8,11,12}）→ LeftAtmos/LeftFdp/RightAtmos/RightFdp（各 3ch×32）
- 延迟：TrebleSurroundDelay（7ch, 30436, stateLen 4348）
- 参数（p5_b0）：MixEqpooliirCoeffs（484 float，默认单位矩阵）、RampCoeff（1, 0.995842）、NumStages（13 uint, 各 10）、TrebleSurroundDelay（1 uint, 1664）

### 3.5 Part4 外围 EQ
- Switch（enable/disable）→ Selector1（22ch 从 32ch: {0,1,2,3,4,5,12,13,14,15,6,7,16,17,8,18,9,19,10,20,11,21}）→ pooliir（iir_accelerator_process, 22ch×32, GLXP IIR, workMem 1104, 13stages×10, RmdlShutdown 时输出零）→ ChannelDelay（dsp.Delay, 22ch, W1_IC_BUFF circBuf 254）
- pooliir：SAS_PeripheralEq_FullRateEq, 工作内存 1104, 22ch, 13 stages
- 参数（p5_b0）：pooliirCoeffs（484 默认单位矩阵）、NumStages（13）

### 3.6 Part5 全息后处理
- SleepingBeauty（4 ramper: currentGain/targetGain/rampCoeff/frameCount, rampCoeffMultipliers=powf(rampCoeff,1:32)）→ channelToRamperMap[22] 映射 → gain×rampCoeffMultipliers 施加到映射通道 → 未用通道置零 → PostHoligramRoutingMap[22] 路由 → FadeControl（2 ramper）→ MuteControl
- 参数（p12_b0）：ChannelToRamperMap（22）、TableDb（30）、TableIdx（30）、Offset（1, 128.0）、RampTime（1, 30.0）、MutesBass（1, 0.0）、PhaseAlignmentDelays（22 uint）

### 3.7 Part6 求和与预放
- Sum（holigram[22×32] + direct[22×32]）→ SpeakerDelay[32384]（22ch, stateLen 1472, per-ch delay）→ MedusaOutputRouter → FadeRamper LPF（TOPFilterCoefficients[3]）→ SpatialFader
- 延迟：SpeakerDelay（22ch, 32384, stateLen 1472）
- 参数（p13_b0）：SpeakerDelay（22 uint）、PostHoligramRoutingMap（22）、FadeControl（TableDb[30] + TableIdx[30] + RampTime + Offset + BoostDisable + EnableSilentExtreme = 64）、FadeRamper（ChannelToRamperMap[22] + DisableLpf + TOPFilterCoeffs[3] = 26）、MuteRampTime（1, 100.0）

### 3.8 PostProcess（后处理）
- PostEQ（pooliir, workMem 1104）→ TestRouter/ASDRouter（MainChannel: mainSelect[22]+mainGain, AuxChannel: AuxSelect[22]+EnableAux, VariableSelector）→ OutputCalibration FreqComp（pooliir, workMem 1104）→ **Limiter**（attack/decay/k1/maxAttack ×高低 2 带 = 16 float）→ **SoftClipper**（p2/xmax/xmin ×2 带 = 6 float）→ PreqOut1[22ch] + AudioOut[32ch]
- pooliir：PostEQ（1104）、OutputCalibrationFreqComp（1104）
- 参数（PostProcess_p0_b0）：ASDRouterMainSelect（22）、AuxSelect（22）、EnableAux（1）、OutputEQpooliirCoeffs（矩阵）、FreqCompxpooliirCoeffs（矩阵）、Limiter（16 = 8×2 带）、SoftClipper（6 = 3×2 带）、OutputSclip（6 = 3×2 带）、MuteRampTime、MuteRampRate、AllMuteRampTime、RmdlMuteRampTime、MuteMapSwitch

### 3.9 Audiopilot35（自适应音频，TID1/3/4/5）
- TID0: 正弦调制（11 点表）→ MatrixMultiply（10×32）→ poolIIR 输入缓冲
- TID1: ref/mic 延迟缓冲（BufferRef 128 + delayBuffer 226 + BufferMic 128 + Buffer 512）
- TID3: 256 点 RFFT（10ch）+ 窗 + FormCoherenceMatrixGXY（HF 噪声估计, Welch 窗）
- TID4: SpeedBounds（128 点速度轴）+ NoiseSlew
- TID5: 30ch 相干求和（Gxx/Gyy/Gyx, magnitude², Saturation）
- pooliir：HF_AntiAliasing（528）、HF_BandpassOrLowpass（528）、LF_FilterMic（96）、LF_FilterRef（96）
- FFT：256 点, 10ch
- 参数（p14_b0）：LatencySamples（3 int）、AntiZipperLpf（3）、BassBpf（6）、BassControl（9）、DyneqBass（2）、EnableRampCoef（1）、HfNoise（18 + pooliirCoeffs）、LfNoise（16 + pooliirCoeffs）、SpeedBounds（3）、HvacTable（数组）

## 4. Pooliir EQ 实例（7 个，GLXP 硬件 IIR 加速器）

| 实例 | 工作内存(floats) | 通道/级数 | 默认系数来源 |
|------|-----------------|-----------|-------------|
| SAS PeripheralEq FullRateEq | 1104 | 22ch, 13stages | p5: MixEqpooliirCoeffs[484]（单位矩阵） |
| PostProcess PostEQ | 1104 | - | PostProcess_p0: OutputEQpooliirCoeffs |
| PostProcess OutputCalibration FreqComp | 1104 | - | PostProcess_p0: FreqCompxpooliirCoeffs |
| Audiopilot HF AntiAliasing | 528 | - | p14: HfNoiseAaFilterpooliirCoeffs |
| Audiopilot HF BandpassOrLowpass | 528 | - | p14: HfNoiseBpLpFilterpooliirCoeffs |
| Audiopilot LF FilterMic | 96 | - | p14: LfNoiseFilterMicpooliirCoeffs |
| Audiopilot LF FilterRef | 96 | - | p14: LfNoiseFilterRefpooliirCoeffs |

> pooliir 系数有静态默认值（TOP 分区初始化），运行期可通过 ASD ID 请求（RMDL 房间模式下载）更新。

## 5. 延迟线（8 条）

| 名称 | 容量(samples) | 通道 | stateLen | 默认延迟 |
|------|--------------|------|----------|---------|
| TrebleDelay | 7928 | 2 | 3964 | - |
| TrebleSurroundDelay | 30436 | 7 | 4348 | 1664 |
| SpeakerDelay | 32384 | 22 | 1472 | per-ch |
| FullRateHoligramDelay | 1760 | - | - | - |
| SASFdpFullRateLeftFastDelay | 2193 | - | 129 | 1161 |
| SASFdpFullRateLeftSlowDelay | 2193 | - | 129 | - |
| SASFdpFullRateRightFastDelay | 2193 | - | 129 | 1161 |
| SASFdpFullRateRightSlowDelay | 2193 | - | 129 | - |

## 6. 参数分区（8 个 TOP 分区）

| 分区 | 标签 | 主要参数量 |
|------|------|-----------|
| PreAmp p0_b0 | InputSelect+PreAmpPart1 | routerOutMap[30] + MakeupGain + InputMixer3dWeights[11] + PreEmpFilterpooliirCoeffs[50] + Bass/Midrange/Treble + Balance(75) + Volume(18) + LevelDetect(12) |
| PreAmp p3_b0 | FDP 系数 | FdpCoeffs(6) + FdpSpum(4) + DirectPathSamplesDec(1161) + TrebleDelay(1280) |
| PreAmp p5_b0 | SAS 混合 | MixEqpooliirCoeffs[484] + RampCoeff(0.995842) + NumStages[13] + TrebleSurroundDelay(1664) |
| PreAmp p8_b0 | Holigram IIR | HoligramIirpooliirCoeffs(矩阵) + NumStages(数组) |
| PreAmp p12_b0 | SleepingBeauty | ChannelToRamperMap[22] + TableDb[30] + TableIdx[30] + PhaseAlignmentDelays[22] |
| PreAmp p13_b0 | Fade/Mute/路由 | FadeControl(64) + FadeRamper(26) + MuteRampTime(100) + PostHoligramRoutingMap[22] + SpeakerDelay[22] |
| PreAmp p14_b0 | Audiopilot | Latency(3) + AntiZipperLpf(3) + BassBpf(6) + BassControl(9) + HfNoise(18+pooliir) + LfNoise(16+pooliir) + SpeedBounds(3) + HvacTable |
| PostProcess p0_b0 | PostProcess | ASDRouter[44] + OutputEQpooliirCoeffs + FreqCompxpooliirCoeffs + Limiter(16) + SoftClipper(6) + Mute(4) |

## 7. const_params 常量数组（16 个 pooled）

- FIR 系数：348 / 351 / 525 / 525 tap（对称低通）
- FFT 窗：2×256
- 索引表：2×36
- 辅助：11 / 34 / 22 / 26 / 80 / 70 / 13

## 8. 子系统层级

```
PreAmp/Selection_HCI_nonHCI_Subsystem/HCI_Content/DecRate/SAS/SAS6System/
├── Part1Bands（输入分带）
├── Part2FdpFullRate（FDP 频域环绕解码）
├── Part3FullRateMixing（全速率混合）
├── Part4FullRatePeripheralEq（外围 EQ）
├── Part5FullRatePostHoligram（全息后处理）
└── Part6SummationAndPreAmp（求与预放）
```

## 9. 二进制文件格式说明

- `.doj` 文件：标准 ELF 可重定位目标文件（`\x7fELF`，32 位小端，机器类型 0x85=EM_ADSP/SHARC），含 .strtab/.symtab，非封闭格式
- `Model_InterpretationEngine64`：TSP（Tunable State Parameter）参数解释引擎，x86 PC 端仿真/调参工具，非音频 DSP 本身
- 核心 DSP 全部在静态 C（`Model_1_1.c` 905KB），非二进制封装

## 10. 组件差距分析（Orpheus 导入对照）

> 将 model_tree 中所有处理块与 Orpheus `components/orpheus/builtin/` 现有组件逐一对照。

### 10.1 已有组件可直接映射（21 项）

| 模型处理块 | Orpheus 组件 | 备注 |
|---|---|---|
| InputSelect（输入路由） | `input_select` | 30->32ch 路由，参数 routerOutMap |
| MakeupGain / Volume | `gain` | 线性增益 |
| Bass BoostCut | `bass` | 低频搁架 |
| Midrange BoostCut | `midrange` | 中频峰值 |
| Treble BoostCut | `treble` | 高频搁架 |
| Balance | `balance` | L/R 平衡 + SilentExtreme |
| LevelDetect | `level_detect` | 电平检测 |
| TrebleDelay / 所有延迟线 | `delay` | 环形缓冲延迟 |
| Windowing（sine+cosine 窗） | `window` | 加窗 |
| Mixer / SumOfElements / Downmix | `mixer` | 2 输入混音（N 输入需级联） |
| FadeControl | `fade` | 频谱前后衰减 |
| MuteControl / MuteRamper | `mute` | 静音 |
| Limiter | `limiter` | 限幅器 |
| SoftClipper | `soft_clipper` | 软削波 |
| Saturation | `saturation` | 饱和 |
| SineMod（正弦调制） | `sine_mod` | 11 点正弦表调制 |
| MatrixMultiply | `matrix_mul` | 矩阵乘（BULK 系数） |
| NoiseSlew | `noise_slew` | 噪声斜坡 |
| Switch / Selector | `switch` | 通道选择 |
| ASDRouter / PostHoligramRouting | `output_router` | 输出路由 |
| Interleave / Deinterleave | `interleave` / `deinterleave` | 通道交织 |

### 10.2 缺失 - DSP 硬件专属（3 项，可理解）

| 缺失组件 | 原因 | Orpheus 近似 |
|---|---|---|
| pooliir / IIR 加速器 | GLXP 硬件 IIR 加速器（13 级×22 通道，workMem 1104） | `biquad_bank` 仅 2 级，需扩展为 N 级 |
| FIR 加速器 | GLXP 硬件 FIR 加速器（348/351/525 tap 对称低通） | `fir` 组件为软件实现，性能不同但功能可覆盖 |
| RFFT / IFFT | 256 点实数 FFT/IFFT（rfft_process_inplace / rifft） | 无 FFT 组件，需新增 |

### 10.3 缺失 - 高级可分解组件（8 项，需深入分析）

| 缺失组件 | 类别 | 可分解度 |
|---|---|---|
| **SleepingBeauty** | 响度补偿 + 多通道增益斜坡 | ★★★ 高（LUT + ramper + 映射） |
| **Rgainx / Rgainy** | 指数增益斜坡器（SleepingBeauty/Fade/Mute 共享基座） | ★★★ 高（gain + 指数平滑） |
| **InputMixer3D** | 3D 输入混音（加权矩阵 + 下混） | ★★★ 高（matrix_mul + mixer） |
| **FDP** | 频域环绕解码（STFT + 系数计算 + 混响提取 + IFFT） | ★★ 中（需 FFT 组件） |
| **Holigram** | 空间音频重建（延迟 + IIR + 路由） | ★★ 中（delay + biquad_bank + router） |
| **FormCoherenceMatrixGXY** | Audiopilot 相干矩阵（交叉 PSD + 高斯消元） | ★ 低（自定义信号分析） |
| **SpeedBounds** | 速度相关噪声界限（查表 + 插值） | ★★★ 高（LUT 组件） |
| **DetectImpulse** | 脉冲检测（能量差 + 阈值） | ★★★ 高（level_detect + switch） |

---

## 11. 高级组件深度分解

### 11.1 Rgainx / Rgainy — 指数增益斜坡器（共享基座）

> SleepingBeauty、FadeControl、MuteControl 底层都使用此组件。是 Orpheus 最应优先补齐的基础设施。

**源码位置**：`blocklib/lib/preamp/rgainx.slx`、`RgainyConfig.m`、`rgainx_Mask.m`

**内部状态**（每个 ramper）：

```c
struct Ramper {
    float currentGain;   // 当前线性增益（平滑跟踪 targetGain）
    float targetGain;    // 目标线性增益
    float rampCoeff;     // 指数斜坡系数（1.0=无斜坡）
    uint32_t frameCount; // 斜坡帧计数
};
```

**算法**（源码 `Model_1_1.c:5039-5230`）：

1. dB 域计算差值：`diff = |20*log10(targetGain) - 20*log10(currentGain)|`
2. 斜坡系数：`factor = log(targetGain/currentGain) / (diff * quantumMs * sampleRateHz)`
3. 每帧更新：`currentGain *= exp(factor)`（指数趋近 targetGain）
4. 静音下限：`currentGain = max(currentGain, 5.01e-7)`（-126 dB，`rgain_SILENT_GAIN`）
5. 通道映射：`ChanToRamperMap[ch]` 决定每个通道用哪个 ramper 的 currentGain 相乘

**参数**：

| 参数 | 类型 | 默认 | 说明 |
|---|---|---|---|
| NumRampers | int | 1 | ramper 数量 |
| ChanToRamperMap | int[] | [1] | 通道->ramper 映射（-1=不处理） |
| InitialLinearGains | float[] | [1.0] | 各 ramper 初始增益 |
| ramp_db_per_second | float | 0 | RTC 斜坡速率（dB/s），0=用 rampTime |
| ramp_milliseconds | float | - | RTC 斜坡时间（ms） |

**Orpheus 分解**：新增 `gain_ramper` 组件 = `gain`（乘法）+ 指数平滑状态机 + 通道映射表。现有 `gain` 的 `update_policy: smoothed` 已有一阶平滑，但缺少 dB 域指数斜坡和多 ramper 独立状态。

---

### 11.2 SleepingBeauty — 响度补偿 + 多通道增益斜坡

> Bose 动态范围/响度补偿算法。根据音量位置（gain_index）应用非对称 L/R 增益锥度，经 4 个指数斜坡器平滑输出。

**源码位置**：`Model_1_1.c:13515-13930`（calculate_SB_gains + calculate_ramp_parameters + control）

**blocklib 配置**：`SleepingBeautyConfig.m`

**三层结构**：

```
gain_index (RTC) ──> [1. TaperGainLUT] ──> cut_linear
                         │
                         v
                    [2. BalanceTaper] ──> targetGains[4] = {left, right, center, mono}
                         │                   gainIdx > offset: left=center=mono=cut, right=1
                         │                   gainIdx < offset: right=center=mono=cut, left=1
                         │                   极端位置: 衰减侧=0, 可选 mute bass
                         v
                    [3. 4x Rgainy] ──> currentGain[4] (指数斜坡)
                         │
                         v
                    [4. ChanToRamperMap] ──> 每通道 × currentGain[map[ch]]
```

**[1] TaperGainLUT** — 30 点锥度增益查表

```c
// TableIdx[30] + TableDb[30] 构成分段查表
// 找到 gainIdx <= TableIdx[j] 的段:
//   首段: cut = (gainIdx / TableIdx[0]) * 10^(TableDb[0]/20)   // 线性插值到零
//   其他: cut = 10^(dB插值 / 20)                               // dB 域线性插值
```

默认表（`SleepingBeautyConfig.m`）：

```
TableIdx = [0, 10, 31, 52, 74, 95, 116, 128, 138, 159, 180, 202, 223, 244, 255]
TableDb  = [-40, -30, -20, -10, 0, 0, 0, 0, 0, 0, 0, -10, -20, -30, -40]
// 典型响度曲线：低音量提升、中音量平直、高音量衰减
```

**[2] BalanceTaper** — 非对称 L/R 锥度

```c
offset = 128;  // 中心位置
delta = gainIdx - offset;
if (delta > 0) {        // 左侧衰减
    left = center = mono = cut_linear;  right = 1.0;
    if (|delta| >= offset-1) { left = center = 0; }  // 极端=完全静音
} else {                // 右侧衰减
    right = center = mono = cut_linear;  left = 1.0;
    if (|delta| >= offset-1) { right = center = 0; }
}
if (极端 && MutesBass) { mono = 0; }  // 可选：极端时静音低音
```

**[3] 4× Rgainy** — 见 11.1，4 个独立 ramper（left/right/center/mono）

**[4] 通道映射** — `ChanToRamperMap[22]`，默认 `[1,2,1,2,3,4,-1,...]`

**参数分区**（p12_b0）：

| 参数 | 量 | 默认 | 说明 |
|---|---|---|---|
| TableDb | 30 float | 见上 | 锥度增益 dB 表 |
| TableIdx | 30 float | 见上 | 锥度增益索引表 |
| Offset | 1 float | 128.0 | 中心位置 |
| MutesBass | 1 float | 0.0 | 极端时是否静音低音 |
| RampTime | 1 float | 30.0 | 默认斜坡时间（ms） |
| ChannelToRamperMap | 22 int | [1,2,1,2,3,4,...] | 通道->ramper 映射 |
| PhaseAlignmentDelays | 22 uint | - | 每通道相位对齐延迟 |

**Orpheus 分解**：

```
gain_index ──> [taper_lut (新)] ──> [balance_taper (新/逻辑)] ──> 4x [gain_ramper (新)] ──> [matrix_mul (通道映射)]
```

可由 3 个新组件构成：`taper_lut`（查表+dB插值）、`gain_ramper`（见 11.1）、`channel_mapper`（通道->ramper 路由）。或合并为单个 `sleeping_beauty` 组件。

---

### 11.3 InputMixer3D — 3D 输入混音

**源码位置**：PreAmpPart1 内，`Model_1_1.c` PreAmp step

**功能**：将多通道输入按权重矩阵混合为立体声 + 514 输出

**处理流**：

```
InputMixer3D:
  AddWeights_514[3] = {LFE+10dB, Lrs2Ls, Rrs2Rs}  // 3 个加权加法
  -> DownmixToStereo:
       Weights_L_R[8]  // 8 通道加权下混为 L/R
  -> Send514Out        // 5.1.4 格式输出
```

**参数**：

| 参数 | 量 | 说明 |
|---|---|---|
| InputMixer3dWeights_514 | 3 float | LFE/Lrs/Rrs 加权 |
| InputMixer3dWeights_L_R | 8 float | 下混立体声权重 |

**Orpheus 分解**：`matrix_mul`（8×N 下混矩阵）+ `mixer`（加权加法）。现有 `matrix_mul` 支持 BULK 矩阵系数，可直接覆盖。514 加权用 3 个 `mixer` 或 `matrix_mul` 的子矩阵。

---

### 11.4 FDP — 频域环绕解码器

**源码位置**：`Model_1_1.c:10182-10900`（Fdp 子系统）

**完整处理流水线**：

```
[输入] L/R 2ch (经 TrebleDelay[7928])
  │
  v
[1] BufferIn: 2ch × 256 样本块（50% overlap）
  │
  v
[2] Windowing: InputOverlap×sine[128] + AudioIn×cosine[128]
  │              (sine/cosine 窗实现 overlap-add 的完美重构)
  v
[3] RFFT: rfft_process_inplace(256点, 2ch -> 129 复数 bin × 2)
  │         (一次复 FFT 算两个实 FFT, SHARC+ cfftf 优化)
  v
[4] Coeffs1stStage (频域系数计算):
  │    absLi=|Lin|, absRi=|Rin|, minAbs=min(|L|,|R|)
  │    Lxk = minAbs / absLi    // 左声道串扰比例
  │    Rxk = minAbs / absRi    // 右声道串扰比例
  │    Lok = 1 - Lxk           // 左直接路径系数
  │    Rok = 1 - Rxk           // 右直接路径系数
  │    SPS = |Lx-Rx| / (|Lx|+|Rx|)  // 空间感指标
  v
[5] LPF 平滑: lsGain=0.90993, lsPole=0.04504
  │              (一阶低通平滑系数，防止系数跳变)
  v
[6] Coeffs2ndStage: Lxks=1-Loks, Rxks=1-Roks
  v
[7] ApplyCoefficients (6 路频域输出):
  │    Lo  = Lok × Lin    Ro  = Rok × Rin     // 直接路径
  │    Lsr = Lxk × Lin    Rsr = Rxk × Rin     // 串扰路径
  │    -> 6ch 频域 {Lo, Ro, Lsr, Rsr, ...}
  v
[8] PSD 平滑: FastPsdSmoothFactor + SlowPsdSmoothFactor
  v
[9] DetectImpulse: EnergyDifference > Threshold ? 脉冲 : 正常
  v
[10] ReverbExtraction (4 条延迟路径):
  │     LeftFast  × FdpDelay[2193](delay=1161)
  │     LeftSlow  × FdpDelay[2193]
  │     RightFast × FdpDelay[2193](delay=1161)
  │     RightSlow × FdpDelay[2193]
  │     (Fast/Slow 代表不同时间常数的混响衰减)
  v
[11] IFFT + OverlapAdd: rifft(256点) -> 时域 -> 叠加 InputOverlap 缓冲
  v
[12] BufferOut: 6ch × 256 -> Selector: 4ch {0,1,2,4}
  │
  v
[输出] 4ch 频域解码环绕
```

**参数分区**（p3_b0）：

| 参数 | 量 | 默认 | 说明 |
|---|---|---|---|
| FdpCoeffs | 6 float | - | FDP 系数（增益/比例） |
| FdpSpum | 4 float | - | SPUM（立体声程序上混）参数 |
| DirectPathSamplesDec | 1 uint | 1161 | 直接路径延迟样本数 |
| TrebleDelay | 1 uint | 1280 | 高频延迟样本数 |

**Orpheus 分解**：

```
delay ─> window ─> [RFFT(新)] ─> [fdp_coeffs(新)] ─> biquad(LPF平滑)
  ─> [reverb_extract(新: 4×delay+gain)] ─> [IFFT(新)] ─> [overlap_add(新)] ─> switch
```

关键缺失：`rfft`/`ifft` 组件（256 点实数 FFT）。FDP 系数计算和混响提取是自定义 MATLAB Function 块，需新建专用组件或用 `matrix_mul` + `delay` 近似。

---

### 11.5 Holigram — 空间音频重建

**源码位置**：Part5 PostHoligram 子系统，`Model_1_1.c`

**组成**：

```
[输入] ──> FullRateHoligramDelay[1760] ──> HoligramIir(pooliir, p8_b0) ──> PostHoligramRouting ──> [输出]
                                             │
                                             └─ GLXP IIR 加速器（可 InitReset/Shutdown）
                                                系数: HoligramIirpooliirCoeffs(矩阵)
                                                级数: HoligramIirPooliirNumStages(array)
```

**功能**：Bose 专有空间声场重建算法。通过延迟 + IIR 滤波 + 路由，在扬声器阵列上重建虚拟声源。

**参数分区**（p8_b0）：

| 参数 | 说明 |
|---|---|
| HoligramIirpooliirCoeffs | IIR 系数矩阵（pooliir 格式） |
| HoligramIirPooliirNumStages | 每通道 IIR 级数数组 |

**RTC 命令**：`FullRateHoligramDisable`（禁用）、`HoligramIirPoolIirGxpAccelInitReset`（初始化）、`HoligramIirShutdown`（关闭）

**Orpheus 分解**：`delay` + `biquad_bank`（扩展为 N 级）+ `output_router`。IIR 部分受限于 pooliir 硬件加速器，软件近似用 `biquad_bank` 级联。

---

### 11.6 FormCoherenceMatrixGXY - Audiopilot 相干矩阵（实证）

**源码位置**：`Model_1_1.c:17628-18160`，子系统 `HFNoiseEstimatorCoh/HfNoiseMusicSeparation`

**功能**：Audiopilot HF 噪声估计核心。用多参考相干（multiple coherence）把麦克风功率谱分离为"音乐（相干）"与"噪声（残余）"两部分，输出噪声功率谱 Gnn。**无需知道扬声器->麦克风传递函数 H**。

**块层级**（5 个 MATLAB Function / S-Function）：
- `FormCoherenceMatrixGXY`(S784)：构建交叉谱矩阵
- `GaussianElimination`(S793)：高斯消元求噪声残余
- `CoherenceModifier`(S792)：高相干压缩
- `RefPowerMin`(S789, TOP_MEX)：噪声下限
- `ExtractMicLevel`(S794) / `NoisePsdLevel`(S796)：电平求和

**实证参数**（从生成代码注释 `<S784>:1` 等读出，非推测）：
- `K = HFWELCHSIZE = 16`（Welch 平均帧数）
- `L = 65` 频率 bin（129 点 RFFT 取奇数 bin 1:2:end，x2 缩放）
- `M = 10` 通道（1 mic + 9 ref）
- GXY = `65x10x10` 复数 CSD 矩阵（6500 creal32_T）
- CoherenceModifier `threshold = 0.88`

**算法（逐块实证）**：

1. **构建 CSD 矩阵**（FormCoherenceMatrixGXY）：帧计数 1..16 循环。每帧对下三角（i<=j）累加 `GXY(:,i,j) += conj(Xi).Xj / K`；第 16 帧用 Hermitian 对称 `GXY(:,i,j)=conj(GXY(:,j,i))` 补全上三角，置 `GnnUpdate=true`。即 GXY 是 16 帧 Welch 平均的交叉谱密度矩阵。

2. **高斯消元 = Schur 补 = 多重相干**（GaussianElimination，仅 GnnUpdate 帧）：对每个 bin 的 10x10 切片做前向行消元（带主元跳过：`a(j,j)/temp(j,j)` 近零则跳过该行）：
   ```
   for j=1..9, i=j+1..10:
     if 主元显著: mult = a(i,j)/a(j,j); a(i,:) = a(i,:) - a(j,:)*mult
   Snn_Gauss = a(N,N)   // 消元后的 (10,10) 元 = 投影掉 ref 后的 mic 残余功率
   Gyy = real(GXY(:,N,N))   // 原始 mic 自谱
   c(i) = 1 - real(Snn_Gauss)/real(Gyy)   // 若 Gyy>eps，否则 c=1
   ```
   `c(i)` 即**平方多重相干**（mic 被 9 个 ref 线性解释的功率比例）。数学上 `Snn_Gauss = Gyy - G_mic,ref . G_ref,ref^-1 . G_ref,mic`（Schur 补）= 噪声残余功率；高斯消元是不显式求逆计算 Schur 补的标准方法。**音乐功率 = c(i).Gyy，噪声功率 = (1-c(i)).Gyy = Snn_Gauss**。

3. **CoherenceModifier**（threshold=0.88）：`if c(i)>0.88: c(i)=sqrt((c(i)-0.88)*(1-0.88))+0.88`。压缩高相干（低噪声）区间，避免低噪声时噪声过估。

4. **噪声 PSD 输出**：`Gnn(i) = max((1-c(i)).Gyy(i), RefPowerMin)`。非更新帧 memcpy 上一帧 Gnn（每 16 帧更新一次）。`NoiseLevel=sum(Gnn)`、`MicLevel=sum(Gyy)` 供下游 AGC/自适应。

**为什么不能"直接减"、为什么要矩阵**：
- "直接减"需逐样本知道扬声器->mic 传递函数 H（未知、时变、带混响），减 raw ref 会留 `(H-1).m` 残留。本方法在**交叉谱域**用矩阵消元把 ref 贡献投影掉，直接得噪声残余 `Snn_Gauss`，**H 隐含在交叉谱里被消掉，无需显式估计**。
- 之所以要**矩阵**而非标量相干：有 9 个 ref 且彼此相关（非正交），必须用矩阵消元/Schur 补一次性投影掉所有相关分量；标量两两相干 `|Gxy|^2/(Gxx.Gyy)` 只能处理 1 个 ref，多 ref 会重复扣除相关分量。

**Orpheus 分解**：复数域交叉谱矩阵 + 高斯消元（Schur 补）+ 阈值压缩，无法用基本组件组合。需新建 `coherence_matrix` 专用组件，依赖 `rfft` 输出（复数 bin），输入 10 通道 STFT、输出 65 bin 噪声 PSD + 多重相干。

### 11.7 SpeedBounds — 速度相关噪声界限

**源码位置**：`Model_1_1.c:19018-19050`

**算法**：

```c
if (SpeedBoundsOn > 0) {
    // interp1: 速度轴[128] -> minDbspl/maxDbspl 查表插值
    minBound = interp1(SpeedBoundsAxis, SpeedBoundsMinDbspl, currentSpeed);
    maxBound = interp1(SpeedBoundsAxis, SpeedBoundsMaxDbspl, currentSpeed);
}
```

**参数**：

| 参数 | 量 | 说明 |
|---|---|---|
| SpeedBoundsOn | 1 float | 使能 |
| SpeedBoundsAxis | 128 float | 速度轴 |
| SpeedBoundsMinDbspl | 128 float | 最小 dB SPL 界限 |
| SpeedBoundsMaxDbspl | 128 float | 最大 dB SPL 界限 |

**Orpheus 分解**：查表插值组件（新 `interp_lut`）。现有无插值查表组件，但逻辑简单（线性插值），可快速实现。

---

### 11.8 DetectImpulse — 脉冲检测

**源码位置**：`Model_1_1.c:11009`

**算法**：

```c
if (EnergyDifference > DetectImpulseThreshold) {
    // 标记为脉冲，切换到脉冲处理路径
}
```

**Orpheus 分解**：`level_detect`（能量计算）+ `switch`（阈值判断切换路径）。可用现有组件直接组合。

---

## 12. 优先级建议

| 优先级 | 组件 | 理由 |
|---|---|---|
| P0 | `gain_ramper`（Rgainx） | SleepingBeauty/Fade/Mute 共享基座，最基础设施 |
| P0 | `rfft` / `ifft` | FDP + Audiopilot 都依赖，无 FFT 整个频域链路缺失 |
| P1 | `sleeping_beauty` | 响度补偿核心，可由 gain_ramper + LUT 组合 |
| P1 | `biquad_bank` 扩展为 N 级 | pooliir 软件近似，Holigram/PeripheralEq/PostEQ 都需要 |
| P2 | `interp_lut`（查表插值） | SpeedBounds + TaperGainLUT 共用 |
| P2 | `fdp`（频域解码） | FDP 专用，依赖 rfft/ifft |
| P3 | `coherence_matrix` | Audiopilot 专用，高度自定义 |
| P3 | `input_mixer_3d` | 可直接用 matrix_mul + mixer 组合 |


## 13. 已实现组件记录

> 以下组件已从 BAF SAS 源码蒸馏并在 Orpheus 中实现，可通过 `orpheus.builtin.<name>` 引用。

### 13.1 gain_ramper（L0 通用原语）

| 项 | 值 |
|---|---|
| 组件 ID | `orpheus.builtin.gain_ramper` |
| 源码对应 | Rgainx / Rgainy blocklib 块 |
| 用途 | 多通道指数增益斜坡器，SleepingBeauty/Fade/Mute 共享基座 |
| 状态 | 已实现，编译通过 |

**设计要点**：
- N 个独立 ramper（1-8），每个维护 `currentGain/targetGain/rampCoeff`
- dB 域恒定速率斜坡：`rampCoeff = ln(target/current) / numBlocks`，每块 `currentGain *= exp(rampCoeff)`
- 通道->ramper 映射（`chan_map` 字符串，-1=bypass）
- 静音下限 -126dB（`GR_SILENT_GAIN = 5.0118723e-7f`，与源码 `rgain_SILENT_GAIN` 一致）
- 每块更新一次 ramper（per-frame，非 per-sample，与源码一致）

### 13.2 iir_bank（L1 专用扩展）

| 项 | 值 |
|---|---|
| 组件 ID | `orpheus.builtin.iir_bank` |
| 源码对应 | pooliir / GLXP IIR 加速器（软件近似） |
| 用途 | N 级级联双二阶 IIR 滤波器组，BULK 系数直写 |
| 状态 | 已实现，编译通过 |

**设计要点**：
- 可配置级数（1-16），BULK 系数连续存储 `coefs[5*16=80]`（双缓冲）
- 每级独立 z1/z2 状态（32 通道上限）
- 级联：`x -> stage[0] -> stage[1] -> ... -> out`
- 系数格式：`[b0,b1,b2,a1,a2] × numStages`，unity 默认（b0=1, rest=0）
- 覆盖场景：Holigram IIR / PeripheralEq / PostEQ / Audiopilot 滤波器

### 13.3 sleeping_beauty（L2 高级组合）

| 项 | 值 |
|---|---|
| 组件 ID | `orpheus.builtin.sleeping_beauty` |
| 源码对应 | `Model_1_1.c:13515-13930` FullRateSleepingBeauty |
| 用途 | Bose 响度补偿算法 |
| 状态 | 已实现，编译通过 |

**设计要点**：
- **TaperGainLUT**：30 点查表，首段线性插值到零、其余段 dB 域插值
- 默认 15 点响度曲线（-40->0->-40 dB，来自 `SleepingBeautyConfig.m`）
- **BalanceTaper**：`gainIndex - offset` 偏移时非对称 L/R 衰减
  - delta > 0：left=center=mono=cut_linear, right=1.0
  - delta < 0：right=center=mono=cut_linear, left=1.0
  - 极端位置（|delta| >= offset-1）：衰减侧=0，可选 `mutes_bass` 静音低音
- **4× ramper**：left/right/center/mono 四路独立指数斜坡（复用 gain_ramper 逻辑）
- **通道映射**：`chan_map` 将通道映射到 4 个 ramper（-1=bypass）
- 参数分区 p12_b0 完整覆盖

### 13.4 分层架构

```
L0 通用原语    gain_ramper (指数增益斜坡基座)
                ↓ 内部复用
L1 专用扩展    iir_bank (N级IIR, BULK系数, pooliir近似)
               rfft / ifft (半复数FFT, radix-2, FDP/Audiopilot)
               input_mixer_3d (加权矩阵混音, BULK权重, 通道数可变)
                ↓ 组合
L2 高级组合    sleeping_beauty (LUT + balance taper + 4x ramper)
```

### 13.5 rfft（L1 专用扩展）

| 项 | 值 |
|---|---|
| 组件 ID | `orpheus.builtin.rfft` |
| 源码对应 | `Model_1_1.c:10182-10900` FDP `rfft_process_inplace`（256 点，2ch） |
| 用途 | 实数 FFT，半复数格式输出；FDP 频域解码 + Audiopilot 分析核心 |
| 状态 | 已实现，编译通过，运行时验证通过 |

**设计要点**：
- radix-2 迭代 FFT（就地位反转 + 蝶形），每通道独立
- 半复数（half-complex）打包：N 个 float 承载 N/2+1 个复数 bin（DC + R(1..N/2-1) + Nyquist + I(N-k) 逆序）
- twiddle 因子 prepare 阶段预计算：`W[k] = exp(-2*pi*i*k/N)`
- FFT 点数 = block_size（须 2 的幂，4~1024），无独立 fft_size 参数；大块配合上游 `downrate`（factor=8 -> 256 点）
- 与 `ifft` 配对完美重构（round-trip identity，已运行时验证）

### 13.6 ifft（L1 专用扩展）

| 项 | 值 |
|---|---|
| 组件 ID | `orpheus.builtin.ifft` |
| 源码对应 | `Model_1_1.c` FDP `rifft`（256 点，6ch）+ OverlapAdd |
| 用途 | 实数 IFFT，半复数格式输入；FDP 频域重建原语 |
| 状态 | 已实现，编译通过，运行时验证通过 |

**设计要点**：
- 利用 `IFFT(X) = (1/N) * conj(FFT(conj(X)))`，复用正向 FFT 核（无独立逆变换代码）
- 解包半复数 -> 完整复数（Hermitian 对称）-> conj -> fft_forward -> conj/N -> 输出实部
- 输入须为 `rfft` 半复数输出格式
- overlap-add 不含在本组件内（由下游 `delay`+`mixer` 组合搭建）
- `rfft -> ifft` 恒等变换（数值精度内，已运行时验证）

### 13.7 input_mixer_3d（L1 专用扩展）

| 项 | 值 |
|---|---|
| 组件 ID | `orpheus.builtin.input_mixer_3d` |
| 源码对应 | `Model_1_1.c` PreAmp InputMixer3D + DownmixToStereo |
| 用途 | 加权矩阵混音器（M 输入 -> N 输出），BULK 权重双缓冲直写 |
| 状态 | 已实现，编译通过，运行时验证通过 |

**设计要点**：
- 权重矩阵 `weights[32x32]` 行优先存储，行步长固定 `IM3D_MAX_CHANNELS=32`（BULK 边界对齐）
- BULK 双缓冲保证权重原子切换；`weights` 字符串参数用于 prepare 期初始化
- 矩阵混音：`out[o] = (sum_i w[o*MAX+i] * in[i]) * gain_linear`
- 输入/输出通道数独立可配（`input_channels`/`output_channels`，均影响签名），支持通道数变化（如 8->2 下混）
- 默认单位矩阵（直通），`gain_db` 平滑更新
- 覆盖 InputMixer3D（5.1.4 加权）与 DownmixToStereo（8ch->L/R）两个块

### 13.8 运行时验证

测试工程 `examples/baf_components_test.yaml` 构建完整链路并通过运行时端到端验证：

```
signal_gen(4ch) -> gain_ramper -> iir_bank -> rfft -> ifft
  -> sleeping_beauty -> input_mixer_3d(4->2ch) -> probe_rms -> wav_out
```

- `cli compile` 通过；plan 正确解析通道数变化（mix 节点 4->2）
- 运行时处理 480000 帧（10s @ 48kHz）无错误，输出 wav 1.92MB
- probe_rms 实测 RMS = 0.192（-6dB 正弦经链路，量级合理）
- rfft->ifft round-trip 恒等验证通过

## 14. 架构演进方向（蒸馏覆盖与多速率建模缺口）

> 后续架构演进重点考虑方向，源自全量组件覆盖核查与多速率 TID 建模分析（2026-08-09）。

### 14.1 组件覆盖现状（四档）

| 档位 | 范围 | 状态 |
|---|---|---|
| 内置可直接映射 | §10.1 的 21 项（input_select/gain/bass/midrange/treble/balance/level_detect/delay/window/mixer/fade/mute/limiter/soft_clipper/saturation/sine_mod/matrix_mul/noise_slew/switch/output_router/interleave 等） | 无需新建 |
| DSP 硬件软件近似 | §10.2：pooliir->iir_bank、FIR 加速器->fir、RFFT/IFFT->rfft/ifft | 已覆盖，非比特一致（GLXP 硬件定点 vs 软件 float） |
| 高级组件已实现 | gain_ramper / iir_bank / rfft / ifft / input_mixer_3d / sleeping_beauty | 已落地 |
| 可组合无需新建 | Holigram（delay+biquad_bank+output_router，§11.5）、DetectImpulse（level_detect+switch，§11.8） | 用现有组件拼 |

### 14.2 仍缺 / 无法只用基本组件复刻

| 缺口 | 性质 | 处置 |
|---|---|---|
| FormCoherenceMatrixGXY（§11.6） | 复数域交叉 PSD `Gxy=X*conj(Y)` + 相干 `Cxy=|Gxy|^2/(Gxx*Gyy)` + 高斯消元，算法层面不可拆解 | 必须新建 `coherence_matrix` 专用组件（依赖 rfft 输出） |
| FDP 自定义块（§11.4） | 系数计算（Lok/Rok/Lxk/Rxk/SPS）、4 路混响提取（fast/slow×L/R 延迟）、overlap-add 重建 | 可用 rfft+matrix_mul+delay+mixer+biquad+ifft 近似拼，缺忠实一体化块；overlap-add 需 delay+mixer 手搭 |
| SpeedBounds（§11.7） | 速度轴查表插值 | 新建 `interp_lut`（逻辑简单） |

### 14.3 滤波器编排无法复刻的三类

1. **相干矩阵分析链**：FormCoherenceMatrixGXY 卡住整个 Audiopilot HF 噪声估计（TID3）。
2. **自适应反馈控制环**：TID1-5 分析结果回灌控制 TID0 参数；`SKILL/references/distill-model.md` §2 明确"当前 Orpheus 不支持图内反馈环"，闭环结构性地无法在图内表达，只能拆任务桥/注释。
3. **多速率 TID 调度**：见 14.4。

### 14.4 为什么蒸馏图没有多条编排线路（根因）

Orpheus 本身支持多速率（`scheduling.divisor` / `downrate`），但蒸馏导入器把所有链压成一条线：

- `orpheus_core/orpheus_core/distill_topology.py:178` `build_topology` 注释"每条链串接为一条主链"；
- `:191`/`:231`/`:232` 链间串联：`sys_in -> chain0 -> chain1 -> ... -> chainN -> sys_out`，链与链是串联非并联；
- `parse_flow` 用 `re.sub(r"^TID\d+\s*:\s*", "", piece)` 剥掉 TID 前缀，建节点不赋 `scheduling.divisor`、不插 `downrate` -> 6 个速率域（1500/750/375/23.4/5.86/1.95Hz）全部坍缩到基础速率。

**结构性校正**：6 个 TID 并非 6 条并行音频通路。按 §2，TID0（1500Hz）是唯一承载音频 input->output 的主路径；TID1-5 是 Audiopilot 分析侧链，逐级降采样做噪声/相干估计，产物是控制参数回灌 TID0，非另一路音频输出。理想形态应为"1 条主音频链 + 若干降速率分析抽头（divisor/downrate）+ 任务桥回灌控制"，而非 6 条并列完整链。

### 14.5 演进重点与落地状态

1. ✅ **build_topology 多速率建模（已落地）**：`task_flows` 结构化规范 + `build_topology` 按 TID 生成降速率分析抽头（`downrate(factor=call_interval)` -> 抽头子模块 -> 分析汇）。BAF SAS 蒸馏现已展开为 TID0 主链 + 5 抽头（÷2/4/64/256/768），主链不再串接分析侧链。
2. ✅ **补不可组合组件（已落地）**：`coherence_matrix`（多参考相干/Schur补）、`psd`（功率谱）、`interp_lut`（查表插值）已实现并接入 `_RULES` 映射；TID3/4/5 分析侧链用真实组件展开（window->rfft->coherence_matrix->psd / interp_lut->noise_slew / mixer->coherence_matrix->square->saturation）。
3. ⬜ **反馈环 / 任务桥（未落地）**：分析抽头止于分析汇（embed_out），分析->控制->主链的回灌仍无法在图内连线（图内禁反馈）。需引入受控"任务桥"原语才能闭环。
4. ⬜ **FDP 自定义块（仍占位）**：ApplyCoefficients 已映射 `matrix_mul`（频域 bin × 系数矩阵）；主链路由/选择块已映射 input_select/output_router。仅余 Coeffs1stStage/Coeffs2ndStage/DetectImpulse/ReverbExtraction 仍为占位（4 处），需一体化 `fdp` 组件或文档化组合方案。

### 14.6 蒸馏现状（2026-08-09 更新）

`examples/baf_sas_full.yaml` 蒸馏模型现已覆盖：

- **分频**：6 个 TID 速率域全部建模（task_flows），导入展开为主链 + 5 降速率抽头；
- **分析**：TID1/3/4/5 分析侧链用真实组件（delay/coherence_matrix/psd/interp_lut/noise_slew/mixer/square/saturation）展开，FDP（TID2）频域链路用 rfft/ifft/window/psd/biquad/matrix_mul；占位块由 9 处降至 4 处
- **未连线**：分析回灌控制（受图内禁反馈限制）；FDP 仍有 4 处自定义块占位（Coeffs1stStage/Coeffs2ndStage/DetectImpulse/ReverbExtraction）。

`build_topology` 展开验证：25 主图节点 / 13 子模块 / 5 downrate 抽头；`test_distill_baf_sas_topology_expansion` 通过；全量 pytest 111 项（蒸馏/子图/平台等纯 Python 用例通过）。
