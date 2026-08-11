# BAF ASM EHC/RNC step0 工程笔记

> 对应文件：`examples/baf_asm_ehc_rnc.yaml`
> 蒸馏来源：`C:\D\Work\Project\EREV\cart-cicd-erev-asm\components\baf` 中的 `Model_Target` 生成 C 代码
> 最后更新：2026-08

---

## 1. 这个工程是什么？

`baf_asm_ehc_rnc.yaml` 是 Bose 车载 ASM（Active Sound Management）系统中 **EHC（Engine Harmonic Cancellation，发动机谐波消除）** 与 **RNC（Road Noise Cancellation，路噪消除）** 的 Orpheus step0 骨架。

源模型是 Simulink 自动生成的 C 代码，运行在 BAF（Bose Audio Framework）单核运行时上。本工程把它蒸馏成可视化图，目标是：

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

以下参数直接取自 `Model_Target_*_TOP.c`，已写入 `examples/baf_asm_ehc_rnc.yaml`：

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
| `sub:rnc_sub/nlms_gain` | `gain_db` | `Rnc_p15_b2.NlmsStepSize` | 全部为 0 → -96 dB（自适应关闭） |
| `sub:rnc_sub/anti_alias_iir` | `coefs` | `Rnc_p15_b0.ReconFilterpooliirCoeffs` | 8ch×6stages pooliir → SOS，经 `scripts/pooliir2sos.py` 转换 |

### 5.2 子图化占位组件

为了尽量不新增专用组件，先把三个核心算法展开为子组件（subcomponent），内部仍用 Orpheus 内置组件占位：

- **`ehc_core`**：封装 `input_router → sine_mod → core_gain → leakage_lpf → harmonic_mix → output_router`。后续把真正的谐波生成/FxLMS 逻辑填进去即可，不必替换为新的原子组件。
- **`rnc_nlms`**：封装 `ref_gain + err_gain → nlms_mix → output_router`。两个输入口对应参考信号与误差信号，输出口接监控。
- **`rnc_control_filter`**：封装 `fir → matrix_mul → output_router`。后续把 Wiener/自适应 FIR 系数灌入即可。

`ehc_sub` 与 `rnc_sub` 中原来的零散节点已替换为这三个子组件节点。

尚未回填的大系数表：

- `Ehc_p0_b1` CoreHmuFreqTable / CoreLeakageFreqTable（查找表，896 元素）
- `Ehc_p0_b2/b3` CoreProjW1/W2/W3/W4（投影表，3584/7168 元素）
- `Ehc_p0_b0` MicAaFilter / ReconFilter / MicConditionHelmholtzFilter pooliir 系数（Bose pooliir 格式，与 Orpheus iir_bank 5-tuple 不兼容）
- `Rnc_p15_b0` Accel/Mic AaFilter、ReconFilter pooliir 系数
- `Rnc_p15_b3/b4` 扬声器-扬声器 / 麦克风-扬声器 Wiener 滤波系数（12800/9600 元素）
- `Rnc_p15_b5` NLMS 自适应滤波初始系数（12000 元素）

这些大表需要专用组件（或脚本转换 pooliir → SOS）才能注入；step0 仍用 identity/占位。

### 5.2 当前占位

| 源算法 | step0 占位 | 后续方向 |
|---|---|---|
| EHC Core 谐波振荡器 + FxLMS | `sine_mod` + `gain` + `mixer` | 实现专用 `ehc_core` 组件或扩展子图 |
| EHC Blade | `iir_bank` + `gain` | 实现窄带误差处理组件 |
| EHC AutoStabilizer | `downrate` + `probe_rms` + `null_sink` | 实现监控/训练逻辑 |
| RNC Downsample | `downrate` + `iir_bank` | 实现多相/抽取滤波 |
| RNC NLMS | `gain` + `mixer` + `probe_rms` | 实现 `nlms` 自适应组件 |
| RNC ControlFilter | `fir` + `matrix_mul` | 实现自适应 FIR + 扬声器映射 |
| RNC SmartSaturation | `limiter` + `soft_clipper` | 实现智能饱和组件 |
| RNC StateMachine | 未显式建模 | 增加多速率监控子图 |

### 5.3 下一步工作

1. **提取 TOP 系数**：从 `Model_Target_Ehc_p0_b*.c`、`Model_Target_Rnc_p15_b*.c`、`Model_Target_Sys_p2_b0.c` 中把表写入对应组件参数。
2. **明确通道语义**：25ch `asm_in` 中哪些是 RPM、扭矩、车速、加速度计、麦克风；22ch `audio_in` 中哪些是座椅/车顶麦克风。
3. **实现真实组件**：`ehc_core`、`ehc_blade`、`rnc_nlms`、`rnc_control_filter` 等。
4. **接入多速率**：TID5/TID6 的 RNC 状态机目前未在图中显式建模，需增加 `downrate` 子图。
5. **控制闭环**：RNC 分析结果目前只能读探针，需让结果能写回 `rnc_gain`/`ehc_gain`。
6. **一致性验证**：与源模型参考输出逐样本对比。

---

## 6. 调试建议

- 先编译：`python -m orpheus_core.cli compile examples/baf_asm_ehc_rnc.yaml`
- 再运行：`python -m orpheus_core.cli run examples/baf_asm_ehc_rnc.yaml`
- 检查 WAV：`outputs/baf_asm_audio_out.wav`（24ch）和 `outputs/baf_asm_ref_out.wav`（18ch）。
- 任务 rate 验证：查看 plan.json 中各节点所属 task 与预期 TID 是否一致。
- 探针：关注 `ehc_sub/autostab_probe`、`rnc_sub/nlms_probe`、`rnc_sub/slow_probe` 是否随输入变化。

---

## 7. 关键文件索引

| 文件 | 作用 |
|---|---|
| `examples/baf_asm_ehc_rnc.yaml` | 本工程主文件 |
| `docs/baf_asm_ehc_rnc_distill_outline.md` | 蒸馏分析与提纲 |
| `components/orpheus/builtin/sine_mod/README.md` | EHC 谐波占位组件说明 |
| `components/orpheus/builtin/fir/README.md` | RNC ControlFilter 占位组件说明 |
| `components/orpheus/builtin/limiter/README.md` | 输出保护组件说明 |
