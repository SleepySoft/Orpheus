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