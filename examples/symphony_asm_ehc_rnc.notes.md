# Symphony ASM EHC/RNC step0 工程笔记

> 对应文件：`examples/symphony_asm_ehc_rnc.yaml`
> 蒸馏来源：`C:\D\Work\Project\EREV\cart-cicd-erev-asm\components\symphony` 中的 `Model_Target` 生成 C 代码
> 最后更新：2026-08

---

## 1. 这个工程是什么？

`symphony_asm_ehc_rnc.yaml` 是 Symphony 车载 ASM（Active Sound Management）系统中 **EHC（Engine Harmonic Cancellation，发动机谐波消除）** 与 **RNC（Road Noise Cancellation，路噪消除）** 的 Orpheus step0 骨架。

源模型是 Simulink 自动生成的 C 代码，运行在 Symphony（Symphony Audio Framework）单核运行时上。本工程把它蒸馏成可视化图，目标是：

1. 对齐顶层 I/O：25 路 `asm_in`、22 路 `audio_in`、24 路 `audio_out`、18 路 `ref_out`。
2. 对齐 7 个同步任务率（TID0~TID6）。
3. 把 EHC/RNC 内部子系统用 Orpheus 内置组件占位，建立可编译、可运行的拓扑基线。
4. 保留 TOP 参数分区元数据，便于后续从 `Model_Target_*_TOP.c` 回填系数。

**当前算法全是占位**：谐波振荡器用 `sine_mod`、FxLMS/NLMS 用 `gain`+`mixer`、自适应滤波器用 `fir`+`matrix_mul`。声音效果不对，但结构和通道数是对的。

---

## 2. 基本配置

| 参数 | 值 | 含义 |
|---|---|---|
| `sample_rate` | 48000 Hz | 系统采样率 |
| `block_size` | 24 | TID1（2 kHz）块长 |
| 基础帧率 | 6000 Hz | TID0，最快基础步 |
| `asm_in` | 25 ch | 混合音频 + 参考/车辆信号 |
| `audio_in` | 22 ch | 麦克风 / 通用音频 |
| `audio_out` | 24 ch | 扬声器输出 |
| `ref_out` | 18 ch | 参考/监控输出 |

### 2.1 任务表

| TID | 周期 | 速率 | 块长 | 主要职责 |
|---|---|---|---|---|
| 0 | 0.1667 ms | 6 kHz | 8 | 最快基础步 |
| 1 | 0.5 ms | 2 kHz | 24 | EHC 主链、RNC 主链、AudioOut32 |
| 2 | 0.6667 ms | 1.5 kHz | 32 | AudioIn / Ref_out |
| 3 | 4 ms | 250 Hz | 192 | EHC 慢速控制（AutoStabilizer 监控） |
| 4 | 5 ms | 200 Hz | 240 | RNC 慢速控制 |
| 5 | 32 ms | 31.25 Hz | 1536 | RNC 状态机 / 发散检测 |
| 6 | 128 ms | 7.8125 Hz | 6144 | RNC 噪声底 / 长期监控 |

---

## 3. 顶层信号流

```
asm_in (25ch @ 2kHz) ──┬──► ehc_sub ──┐
                       ├──► rnc_sub ──┼──► output_processing ──► audio_out (24ch @ 2kHz)
audio_in (22ch @1.5kHz)┘              │                          ref_out   (18ch @1.5kHz)
                                      ▼
                          ent_in (asm_in 直通) + ref_in (audio_in 直通)
```

- `asm_in` 同时作为 EHC 输入、RNC 输入、以及娱乐音直通源。
- `audio_in` 作为 `ref_out` 的参考源，也作为 RNC 误差麦克风概念的占位输入。
- `output_processing` 把 EHC/RNC 反噪声与娱乐音混合，经 limiter/soft_clipper 后输出。

---

## 4. 子系统详解

### 4.1 `ehc_sub` — Engine Harmonic Cancellation

源模型中 EHC 包含谐波振荡器、FxLMS 核心、Blade 窄带处理、AutoStabilizer、AutoEnhancer、MicConditioning 等。step0 做了极度简化：

- `input_router`：从 25ch `asm_in` 中选出 8 路作为 EHC 工作通道（索引 0~7，后续应根据 FD 路由表回填）。
- `sine_mod` + `core_gain`：占位谐波振荡器，产生与输入同通道数的正弦信号。
- `core_mix`：把谐波信号与输入混合，模拟 FxLMS 的输出叠加。
- `blade_iir` + `blade_gain`：占位 Blade 窄带误差麦克风滤波。
- `output_mix` + `output_router`：合并 Core/Blade 输出并路由。
- `downrate_to_tid3` + `autostab_*`：把 EHC 输出降采样到 TID3（250Hz），用 `probe_rms` 监控，对应 AutoStabilizer 的慢速监控支路。

### 4.2 `rnc_sub` — Road Noise Cancellation

源模型中 RNC 包含输入处理、降采样、NLMS 自适应、ControlFilter、SmartSaturation、StateMachine 等。step0 占位：

- `input_router`：从 25ch 中选出 8 路（与 EHC 类似，后续应独立映射）。
- `anti_alias_iir` + `rnc_gain`：占位抗混叠与输入缩放。
- `nlms_gain` + `nlms_mix` + `nlms_probe`：NLMS 系数更新占位，监控 RMS。
- `control_fir` + `control_matrix`：ControlFilter 占位，对参考信号做 FIR 后矩阵混音。
- `output_mix` + `output_router`：合并并输出 8 路反噪声。
- `downrate_to_tid4` + `slow_*`：RNC 慢速监控支路，落在 TID4。

### 4.3 `output_processing` — 输出混合与保护

- `ehc_gain` / `rnc_gain`：分别衰减 EHC/RNC 反噪声，防止叠加后过大。
- `ent_sel`：从 25ch `asm_in` 中选出 8 路作为娱乐音。
- `mix1`：EHC + RNC。
- `mix2`：(EHC+RNC) + 娱乐音。
- `limiter` + `soft_clipper`：输出保护。
- `final_router`：8→24 扩展，identity 填充前 8 路，后 16 路为零。
- `ref_gain` + `ref_router`：22→18 选择，对应 `Model_Ref_out`。

---

## 5. 占位与后续工作

### 5.1 已从 TOP 文件回填的参数

以下参数直接取自 `Model_Target_*_TOP.c`，已写入 `examples/symphony_asm_ehc_rnc.yaml`：

| 节点 | 参数 | 来源 | 说明 |
|---|---|---|---|
| `sub:ehc_sub/input_router` | `indices` | `Ehc_p0_b0.PassthroughIndicesSelector` | `[1,2,3,4,1,2,3,4]`（1-based）→ `0,1,2,3,0,1,2,3` |
| `sub:rnc_sub/input_router` | `indices` | `Rnc_p15_b0.PassthroughIndicesSelector` | `[1,2,3,4,5,6,1,2]` → `0,1,2,3,4,5,0,1` |
| `sub:output_processing/ent_sel` | `select` | `Sys_p2_b0.EhcEntMixChannelSelect` | `[1,2,3,4,5,6,13,14]` → 娱乐音通道选择 |
| `sub:output_processing/ehc_gain` | `gain_db` | `Sys_p2_b0.EhcEntMixGain` | 1.0 线性 → 0 dB |
| `sub:output_processing/rnc_gain` | `gain_db` | `Sys_p2_b0.RncCombineSignalGain` | 1.0 线性 → 0 dB |
| `sub:output_processing/limiter` | `threshold_db` | `Rnc_p15_b2.SsSpeakerOutputLim` | 2.0 线性 → 6 dB |
| `sub:ehc_sub/sine_mod` | `freq_hz/depth` | `Ehc_p0_b0.OnOff=0` | EHC 关闭，freq=0、depth=0 时输出直通 |
| `sub:ehc_sub/blade_gain` | `gain_db` | `Ehc_p0_b0.BladeMicMuMuliplierLimitLow` | 0.1 线性 → -20 dB |
| `sub:rnc_sub/rnc_nlms` | `step_size` | `Rnc_p15_b2.NlmsStepSize` | 0.01 占位，后续按源模型标定量回填 |
| `sub:rnc_sub/anti_alias_iir` | `coefs` | `Rnc_p15_b0.ReconFilterpooliirCoeffs` | 8ch×6stages pooliir → SOS，经 `scripts/pooliir2sos.py` 转换 |

### 5.2 子图化占位组件

为了尽量不新增专用组件，先把三个核心算法展开为子组件（subcomponent），内部仍用 Orpheus 内置组件占位：

- **`ehc_core`**：封装 `input_router → sine_mod → core_gain → leakage_lpf → harmonic_mix → output_router`。后续把真正的谐波生成/FxLMS 逻辑填进去即可，不必替换为新的原子组件。
- **`rnc_nlms`**：已替换为 `orpheus.builtin.nlms` 原子组件，直接接收 `ref`（参考信号）和 `err`（误差信号），输出 `out` 供监控。保留 `rnc_nlms` 节点 id 以维持现有连接。
- **`rnc_control_filter`**：封装 `fir → matrix_mul → output_router`。后续把 Wiener/自适应 FIR 系数灌入即可。

`ehc_sub` 与 `rnc_sub` 中原来的零散节点已替换为这三个子组件节点。

尚未回填的大系数表：

- `Ehc_p0_b1` CoreHmuFreqTable / CoreLeakageFreqTable（查找表，896 元素）
- `Ehc_p0_b2/b3` CoreProjW1/W2/W3/W4（投影表，3584/7168 元素）
- `Ehc_p0_b0` MicAaFilter / ReconFilter / MicConditionHelmholtzFilter pooliir 系数（Symphony pooliir 格式，与 Orpheus iir_bank 5-tuple 不兼容）
- `Rnc_p15_b0` Accel/Mic AaFilter、ReconFilter pooliir 系数
- `Rnc_p15_b3/b4` 扬声器-扬声器 / 麦克风-扬声器 Wiener 滤波系数（12800/9600 元素）
- `Rnc_p15_b5` NLMS 自适应滤波初始系数（12000 元素）

这些大表需要专用组件（或脚本转换 pooliir → SOS）才能注入；step0 仍用 identity/占位。

### 5.3 当前占位

| 源算法 | step0 占位 | 后续方向 |
|---|---|---|
| EHC Core 谐波振荡器 + FxLMS | `ehc_core` 子图 | 在子图内实现谐波生成/FxLMS |
| EHC Blade | `iir_bank` + `gain` | 实现窄带误差处理组件 |
| EHC AutoStabilizer | `downrate` + `probe_rms` + `null_sink` | 实现监控/训练逻辑 |
| RNC Downsample | `downrate` + `iir_bank` | 实现多相/抽取滤波 |
| RNC NLMS | `orpheus.builtin.nlms` | ✅ 已实现为可复用原子组件 |
| RNC ControlFilter | `rnc_control_filter` 子图 | 在子图内实现自适应 FIR + 扬声器映射 |
| RNC SmartSaturation | `limiter` + `soft_clipper` | 实现智能饱和组件 |
| RNC NoiseFloor | `rnc_noise_floor` 子图 | 在子图内实现 STFT + 噪声底估计 |
| RNC DivergenceDetector | `rnc_divergence_detector` 子图 | 在子图内实现发散检测 |

### 5.4 RNC 状态机/噪声底/发散检测路径（TID5/TID6）

已在顶层补全两条慢速分析链：

| 链 | 任务 | 输入 | 当前占位输出 | 源模型对应 |
|---|---|---|---|---|
| `rnc_noise_floor` | TID5 31.25 Hz | `asm_in` 前 12 路 → `accel_select` → `nfd_downrate` (÷64) | `nf_est`(12ch)、`faulty`(12ch)、`freeze_counter`(1ch) | `RncSubTID5` 中的 `NoiseFloorEstimation`：12 路加速度计 STFT → 噪声底估计/故障检测 |
| `rnc_divergence_detector` | TID6 7.8125 Hz | `audio_in` 前 4 路 → `roof_select` → 二级降采样 (÷64÷4)；`rnc_sub:out` → 二级降采样 (÷64÷4) | `divergence_flag`(1ch)、`step_size_modifier`(1ch)、`importance_factor`(8ch) | `RncSubTID6` 中的 `DivergenceDetector`：车顶麦克风 + 扬声器参考 → 发散检测 |

注意：
- 当前 `accel_select`/`roof_select` 用的是前 N 路占位索引，真实语义需对照 `ActiveAccelChannelsMap`、`ActiveRoofMicsMap`、`DdSelectedRoofMics` 回填。
- TID5/TID6 已实现 `circular_buffer → window → rfft → spectral_reduce` 的 STFT 功率谱路径：
  - `rnc_noise_floor`：12 路加速度计 → 128 点 FFT → 每通道取最小功率 → `nf_est`。
  - `rnc_divergence_detector`：4 路车顶麦克风 → 256 点 FFT → 每通道平均功率 → 与扬声器耦合信号混合。
- 已启用 50% 重叠 Hann 窗：`rnc_noise_floor`（128 点 FFT，hop=64，num_frames=24）和 `rnc_divergence_detector`（256 点 FFT，hop=128，num_frames=48）。`circular_buffer` 的 `hop_size < frame_size` 与 `window` 的 `repeat` 模式配合实现滑窗加窗。
- 两条链的输出目前只接到子组件内部的 `probe_rms`/`null_sink`，没有真正闭环控制 `rnc_gain`/`ehc_gain`。

### 5.5 下一步工作

1. **~~新增可复用原子组件~~** ✅：
   - `orpheus.builtin.circular_buffer`：支持重叠的循环缓冲/滑窗分帧。
   - `orpheus.builtin.window`：新增 `repeat` 模式，可对多帧连续加窗。
   - `orpheus.builtin.rfft`：新增 `fft_size` 与 `output_mode`（magnitude/power），支持一帧内处理多个 FFT 帧。
   - `orpheus.builtin.spectral_reduce`：对 STFT 功率谱做 sum/mean/min/max 聚合。
2. **用原子组件拼出项目子图** ✅：
   - `rnc_noise_floor` 已用 `circular_buffer → window → rfft → spectral_reduce` 替换 `probe_rms` 占位。
   - `rnc_divergence_detector` 已用同样链路替换车顶麦克风探针。
3. **提取 TOP 系数**：从 `Model_Target_Ehc_p0_b*.c`、`Model_Target_Rnc_p15_b*.c`、`Model_Target_Sys_p2_b0.c` 中把表写入对应组件参数。
4. **明确通道语义**：25ch `asm_in` 中哪些是 RPM、扭矩、车速、加速度计、麦克风；22ch `audio_in` 中哪些是座椅/车顶麦克风。
5. **实现真实子图**：`ehc_core`、`ehc_blade`、`rnc_control_filter`、`rnc_noise_floor`、`rnc_divergence_detector`；`rnc_nlms` 已完成。
6. **控制闭环**：让 `rnc_noise_floor` 的 `nf_est`、`rnc_divergence_detector` 的 `divergence_flag` 等能够调制 `rnc_gain`/`ehc_gain`。
7. **一致性验证**：与源模型参考输出逐样本对比。

---

## 6. 调试建议

- 先编译：`python -m orpheus_core.cli compile examples/symphony_asm_ehc_rnc.yaml`
- 再运行：`build/orpheus_runtime.exe examples/symphony_asm_ehc_rnc.plan.json build/components`
- 检查 WAV：`outputs/symphony_asm_audio_out.wav`（24ch）和 `outputs/symphony_asm_ref_out.wav`（18ch）。
- 注意：当前 step0 占位工程在运行时会因 `ehc_sub` 的 AutoStabilizer 支路（与本次改动无关）发生访问冲突，属于已知问题；编译和 STFT 链路单元测试已通过。
- 任务 rate 验证：查看 plan.json 中各节点所属 task 与预期 TID 是否一致。
- 探针：关注 `ehc_sub/autostab_probe`、`rnc_sub/nlms_probe`、`rnc_sub/slow_probe` 是否随输入变化。

---

## 7. 关键文件索引

| 文件 | 作用 |
|---|---|
| `examples/symphony_asm_ehc_rnc.yaml` | 本工程主文件 |
| `docs/symphony_asm_ehc_rnc_distill_outline.md` | 蒸馏分析与提纲 |
| `components/orpheus/builtin/sine_mod/README.md` | EHC 谐波占位组件说明 |
| `components/orpheus/builtin/fir/README.md` | RNC ControlFilter 占位组件说明 |
| `components/orpheus/builtin/limiter/README.md` | 输出保护组件说明 |
| `components/orpheus/builtin/nlms/README.md` | NLMS 自适应滤波组件说明 |

---

## 8. 非音频数据链路审计与下一步（2026-08-26）

> 背景：系统已落地「非音频数据链路」（控制链路 control_connections，
> 见 docs/design_control_link_eval.md，commit 122d674）。在 EHC/RNC 示例上
> 重新审视「完善」的范围，并记录一条关键设计决策。

### 8.1 现状核查（实证）

- 该示例最后一次修改早于控制链路特性（abd8f7b），因此 examples/symphony_asm_ehc_rnc.yaml **没有使用新的 control_connections 机制**。
- 当前直接 compile 即失败：block size mismatch at rnc_divergence_detector__error_mix: inputs differ (8192 vs 6144)。发散检测子图把 STFT 谱输出（sr）与耦合矩阵输出（coupling_matrix）在同一个 error_mix（mixer）混音，两者有效块长不一致，已退化为编译阻断。
- 诊断/反馈信号（nf_est、divergence_flag、step_size_modifier、importance_factor、STFT 谱）目前全部接到 probe_rms / null_sink 死胡同，没有闭环——这正是非音频数据链路（feedback + 矩阵分析结果）要解决的场景。
- 文档/元数据里 distilled_from: ...components/symphony 指向的目录已不存在：原 components/symphony 已整体迁移到 components/baf，真实生成码在 baf\src\out\baremetalgul\slx\code\Model_Target_ert_shrlib_rtw\ 下（Model_Target*.c、EHC.c/.h、RncSub.c/.h、OutputRouter.c、SignalSplitter.c，以及 Model_Target_Ehc_p0_b*_TOP.c / Model_Target_Rnc_p15_b*_TOP.c 等 TOP 分区，与 yaml model_tree.parameter_partitions 一一对应）。

### 8.2 关键设计决策：子图可导出「控制连接点」

设计要点：**子图（subcomponent）不仅导出音频连接点（port），也可导出控制连接点。**即子图 ports 除音频 direction/maps_to 之外、
更新增加「控制连接点」。这样，跨子图的控制连接（cross-subgraph control link）是自然的：顶层 control_connections 直接引用 <sub_id>:<control_port>，子图 flatten 时把控制点解析为内部原子节点参数（与音频 maps_to 同一机制横向贯通）。

示例（子图 ports 增加控制点）：

```yaml
subcomponents:
  - id: ehc_sub
    ports:
      - id: autostab_level
        kind: control_output
        maps_to: autostab_probe:rms
      - id: gain_override
        kind: control_input
        maps_to: core_gain:gain_db
```

由若 EHC/RNC 反馈闭环显式化：EhcSub.AutosStabilizerProbe.rms（control_source）→ EhcSub.core_gain.gain_db；RncSub.slow_probe.rms → RncSub.rnc_gain.gain_db；RncNoiseFloor.nf_est → Rnc 饱和/权重阈值；RncDivergence.importance_factor / step_size_modifier → RncNlms 步长。
边界：控制边值在数字型时为标量（当前实现）；count>1 的数值数组链路仍走 bulk 校验，需运行时/生成侧确认后接入。

### 8.3 完善工作清单（按优先级）

1. **修复编译阻断**：对齐 rnc_divergence_detector 的 error_mix 有效块长，恢复可编译、可运行。
2. **接入控制链路**：把可标量化的诊断参数用 control_connections 闭环（至少一条演示闭环），让非音频数据链路真正生效。
3. **补齐子图控制端口能力**：实现/验证「子图导出控制点」，使跨子图控制连接成为一等语法。
4. **同步 baf 路径**：更新 yaml / notes / 蒸馏提纲中的 components/symphony 引用为 components/baf/...。
5. **一致性验证**：与参考模型逐样本对比（保持双路径一致性测试基线）。


---
### 8.4 rate_sync：多速率异步合流组件（2026-08-26）

#### 问题澄清
`rnc_divergence_detector` 有两个独立输入端口：`roof_in`（来自车顶麦 `audio_in`，
TID2、1.5kHz、基块 32）与 `spkr_in`（来自 `rnc_sub:out`，TID1、2kHz、基块 24）。
两条路径各自降到同一慢速（约 7.8Hz），但：
- **采样率不同**（1.5kHz 与 2kHz，需各自 resample 到同域）；
- **批次/调度不同**（divisor 192 与 256）；
- **子图内部 `error_mix` 想把两者的处理结果在同一音频 mixer 节点合流**。

在真实模型里这是合法的：发散/cm 诊断就是要比较"预测(基于 spkr 估计) vs 实测(roof 麦)"。
但这依赖一个**跨速率域的同步缓冲**，把两条已分别降到同一慢速的流，按同一 tick 相位对齐后再合并。

此前清除 hack：误把 `roof_select` 源从 `audio_in` 改为 `asm_in`（强行让整除 divisor 相等）来"绕开"编译，
但那会**把车顶麦克风语义替换成别的信号**，虽能编译、运行无意义——已还原。见下方"应做/不应做"。

#### 根因：divisor 定义与传播
- `scheduling.divisor` 定义在**组件 manifest**（如 `downrate` 的 `param:factor`）。
- 编译器 `_propagate_rate_divisors` 将其沿"输出端口"正向传播：`port_divisor[node:out] = d_in * factor`。
- **汇合校验在编译器层面一刀切**：某节点所有输入端口收集到的 divisor 若 `len(set)>1` 即报
  `rate mismatch` / `block size mismatch`——它不区分该节点是否本应接受不同速率的输入。
- 因此"divisor"在数据流上是**每条输入链的边属性**；把两条不同基块链硬塞进一个 audio mixer，
  在现有模型下过不了编译，**这不是语义错误、而是模型表达不全**。

#### 设计决策
- 新增 **`orpheus.builtin.rate_sync`** 组件：多输入 FIFO 同步缓冲。
  - 作用：把两条**已分别降到同一慢速（同绝对采样率、同 tick 块长）** 但相位不一致的流，缓冲对齐到同一 tick 后合流输出。
  - 参数：`channels`、`mode`（auto/lcm/fixed）、`block_a`/`block_b`（或 `block_sizes`）、`buffer_length`（可调下限）、`insert_at`（可当作延迟）。
  - **自动分析 buffer 长度**：按输入各支路块的 LCM（最小公倍数，默认建议）自动选一个能容纳整帧对齐的缓冲深长；亦可视输  phase 之差手动指定。
  - 输出：合流后的对齐数据（同速率）。
- 编译器配套（最小改动）：在 rate_sync 的 manifest 用 `scheduling.merge: true` 声明"接受异步多速率输入"，
  `_propagate_rate_divisors` 对该节点**跳过输入集合相等**强校验（但仍执行拓扑/时钟可用）。
  从而两条链能同时进入同一组件。

#### 教训（不要重蹈）
- 不要用"改信号源让整除 divisor 相等"来绕过编译器——那改变语义。
- "合流"必须由组件自身持有缓冲（FIFO）并在输入边界对齐，而不是靠编译器把两个块硬塞进 mixer。
- 本组件完成后，`rnc_divergence_detector` 的 `error_mix` 应改为用 rate_sync 对齐后合流（语义才真）。
