# Symphony SAS step0 工程笔记

> 对应文件：`examples/symphony_sas_step0.yaml`  
> 蒸馏来源：`Model_1_1.c`（Symphony Symphony SAS 空间音频系统）  
> 作者/维护者：Orpheus 团队  
> 最后更新：2026-08

---

## 1. 这个工程是什么？

`symphony_sas_step0.yaml` 是 Symphony SAS（Symphony Automotive Framework - Spatial Audio System）在 Orpheus 中的**第 0 步骨架工程**。它把整个 `Model_1_1.c` 的音频处理链路拆成了若干子组件（subcomponents），用 Orpheus 的原子组件先搭出**拓扑结构、通道数、延迟线位置和参数分区**，但大量系数和复杂算法目前还是占位实现。

核心目标不是“听起来对”，而是：

1. **建立完整的信号流图**：从多通道测试信号源 → FDP/混音/EQ/后处理 → 22 通道主输出 + 10 通道 Audiopilot 输出。
2. **验证子组件展开和通道路由**：确认 30→12→32→22 等复杂通道转换在编译器和运行时都能正确连线。
3. **为后续回填真实系数和算法提供可运行的基线**：所有子系统的接口、延迟、任务分区都已经和 `Model_1_1.c` 对齐。

### 1.1 基本配置

| 参数 | 值 | 含义 |
|---|---|---|
| `sample_rate` | 48000 Hz | 系统采样率，与车载音频系统一致 |
| `block_size` | 32 | 每次 process 调用处理 32 个样本 |
| 基础帧率 | 1500 Hz | `48000 / 32`，即 `model_tree.base_rate` |
| 输出通道 | 22 路 | 主输出（`main_out` / `main_probe`） |
| Audiopilot 输出 | 10 路 | 噪声补偿侧链输出（`ap_out` / `ap_probe`） |

> 为什么是 1500 Hz？在 `Model_1_1.c` 中，大量控制逻辑和混音矩阵以 1500 Hz 的“全速率任务”运行；FDP、Audiopilot FFT 等则以 750/375/23.4/5.86/1.95 Hz 等分速率运行。step0 目前只跑了 TID0 主链，分速率任务尚未在图中显式建模。

---

## 2. 信号流总览

用一句话概括：

```
测试信号源（music/treble/mono/treble_surround）
    → Part2 FDP（占位） → Part3 混音 → Part4 外围 EQ → Part5/6 全息求和
    → feedback_delay/expand 反馈
    → music_mixer 合并
    → input_select（30→12）
    → pre_amp（预放/响度/音量）
    → post_process（EQ/限幅/软削/校准/延迟）
    → main_out（22 路 WAV + probe）
    
pre_amp 还分出 buffer1/buffer2 → audiopilot（噪声补偿侧链）
part2_fdp 还分出 fdp_lo_ro → audiopilot
```

顶层图中有几个值得注意的设计：

- **`music_src` 是 30 路伪输入**：它不代表真实 30 通道音频，而是把反馈回来的 22 路扩展成 30 路后，与“原始音乐”合并的位置。`music_mixer` 把 `music_src` 和 `feedback_expand` 的输出加在一起。
- **反馈环**：`part5_6:out` → `feedback_delay` → `feedback_expand` → `music_mixer`。这是 Symphony 的“前馈-反馈”结构的一部分，用于把后处理后的信号重新注入前端。
- **`fdp_tap`**：从 `part2_fdp:buffer_out`（6 路）中挑选 4 路（索引 1,2,3,5）送给 `audiopilot:fdp_lo_ro`，对应真实 FDP 双速率接口中的 Selector(6ch→4ch:{0,1,2,4})，注意这里的 0-based 索引在 YAML 中写成了 1,2,3,5。
- **Audiopilot 侧链**：接收 `pre_amp:buffer1`（10 路）、`pre_amp:buffer2`（1 路）、`mono_src`（1 路）和 `fdp_tap`（4 路），输出 10 路宽频噪声补偿信号。

---

## 3. 子系统详解

### 3.1 `part2_fdp` — Symphony Part2 频域环绕解码（当前占位）

**真实作用**：把 2 路高频（treble_lr）通过 STFT/混响/系数矩阵解码成 6 路环绕信号（Lo/Ro/Lsr/Rsr + 2 路混响），是全系统的“频域全景声”核心。

**当前实现**：

- `treble_delay`：2 通道延迟线，延迟 1280 样本（≈26.7ms），对应 `TrebleDelay`。
- `fdp_matrix`：2→6 的 `matrix_mul`，但矩阵目前是 `[[1,0],[0,1],[0,0],[0,0],[0,0],[0,0]]`，基本只是把左右声道原样输出到前 2 路，其余为 0。

**为什么这样**：真实的 FDP 需要 256 点 FFT、重叠相加、4 条混响延迟线（各 2193 样本）以及复杂的相干系数计算。step0 先把延迟和 6 路输出形状对齐，后续会把 `fdp_matrix` 替换成真正的频域处理子图或专用组件。

**连接**：

- 输入：`treble_lr`（2ch）
- 输出：`buffer_out`（6ch）→ `part3_mixing:buffer_out` 和 `fdp_tap`

---

### 3.2 `part3_mixing` — 全速率混音

**真实作用**：把 FDP 输出的 6 路缓冲、7 路 treble_surround 延迟信号，通过通道扩展、EQ、三组 `slc_matrix_mul`（Cs/Left/Right）混音成 22 路输出。

**当前实现**：

- `buf_expand`：6→13 路扩展，目前用 `identity`，前 6 路直通，后 7 路置零。
- `ts_delay`：7 通道延迟线，延迟 1664 样本（≈34.7ms），对应 `TrebleSurroundDelay`。
- `ts_expand`：7→13 路扩展，矩阵把 7 路 surround 映射到 13 路空间中的特定位置。
- `concat`：把 `buf_expand`（13ch）和 `ts_expand`（13ch）两路合并成 13ch。
- `mix_eq`：13 通道 10 阶 IIR bank，目前系数全为单位矩阵（直通）。真实 `MixEqpooliirCoeffs` 有 484 个 float。
- `cs_mix` / `left_mix` / `right_mix`：三个 `slc_matrix_mul`，目前只有单张表且近似单位矩阵，未启用 N 表插值。
- `cs_expand` / `left_expand` / `right_expand`：把 2/10/10 路输出扩展成 22 路，按 Symphony 的合并位置填入。
- `merge1` / `merge2`：把三组 22 路扩展结果加起来。

**设计要点**：

- 三组 `slc_matrix_mul` 是 Symphony Part3 的核心：`Cs`（13×2，控制中央/单声道能量）、`Left`（13×10，左半空间）、`Right`（13×10，右半空间）。step0 目前用单表占位，保留了 `ramp_coeff=0.995842` 的默认斜坡系数。
- `mix_eq` 的 13 通道 × 10 阶 IIR 对应 `MixEqpooliirCoeffs`，后续会从 TOP 文件回填。

---

### 3.3 `part4_peripheral_eq` — 外围 EQ

**真实作用**：22 路信号的开关、通道重排序、13 阶每通道 IIR EQ、通道延迟。

**当前实现**：

- `enable`：`switch` 组件，默认打开（`enable=1`）。
- `reorder`：22→22 的 `input_select`，重排通道顺序。索引表 `1,2,3,4,5,6,13,14,15,16,7,8,17,18,9,19,10,20,11,21,12,22` 对应 `Model_1_1.c` 中的 `Selector1`。
- `full_rate_eq`：22 通道 13 阶 IIR bank，系数未填，默认直通。
- `ch_delay`：22 通道延迟线，目前所有通道延迟 0 样本。

**注意**：`full_rate_eq` 是 `PeripheralEq_FullRateEq` pooliir 的占位，真实有 484 个系数（22×13×?）。`ch_delay` 对应 `PhaseAlignmentDelays` 等延迟表，后续回填。

---

### 3.4 `part5_6` — 全息后处理与求和

**真实作用**：Symphony Part5（SleepingBeauty/PostHoligramRouting/FadeControl/MuteControl） + Part6（Sum/SpeakerDelay/SASRouting/FadeRamper/SpatialFader）。这是把 22 路“直接声”和“全息声”混合、加扬声器延迟、做前后衰减的最后一步。

**当前实现**：

- `split`：22→22 通道路由，目前identity，把输入同时送给 `sleeping` 和 `sum:in0`。
- `sleeping`：`sleeping_beauty` 组件，默认 `gain_index=128`（中心位置），22 路全部映射到 ramper 0（目前无效，因为所有通道都是 0 dB）。
- `post_holo_route`：22→22 通道路由，identity。
- `fade_ctrl`：`gain_ramper`，默认 0 dB，22 路映射到 ramper 0。
- `mute_ctrl`：`mute` 组件，默认不静音。
- `sum`：把 `split` 的直接信号（`in0`）和 `mute_ctrl` 出来的全息信号（`in1`）相加。
- `speaker_delay`：22 通道延迟线，最大 32384 样本，目前全 0。
- `sas_router`：22→22 通道路由，identity。
- `fade_ramper`：`gain_ramper`，默认 0 dB。
- `spatial_fader`：`fade` 组件，`front_channels=11`，默认 `fade=0`（平直）。

**为什么 split 同时送两路**：在真实系统中，Part5 处理的是“全息/环绕”分支，Part6 的 `sum` 把它和“直接声”分支合并。step0 为了保留结构，用同一个 `split` 输出同时充当直接声，等后续有了真正的 PostHoligramRouting 后再改。

---

### 3.5 `input_select` — 输入选择

**真实作用**：从 30 路输入中选出 12 路送给预放。对应 `InputSelect` 和 `Variable Selector`。

**当前实现**：

- `ent_sel`：`input_select` 组件，`channels_in=30`，`channels_out=12`，选择索引 `1,2,3,4,5,6,7,8,9,10,11,12`（即前 12 路）。

**注意**：真实 `routerOutMap[30]` 或 `REQ_routerMap[30]` 会根据调音模式选择不同输入，step0 固定取前 12 路。

---

### 3.6 `pre_amp` — 预放

**真实作用**：MakeupGain → InputMixer3D（5.1.4 加权） → DownmixToStereo → Bass/Midrange/Treble → Balance → Volume → PreEmp IIR → LevelDetect。同时产生 `buffer`（32 路主输出）、`buffer1`（10 路给 Audiopilot）、`buffer2`（1 路给 Audiopilot）。

**当前实现**：

- `makeup`：12 路 `gain`，默认 0 dB。
- `input_mixer`：12→12 `input_mixer_3d`，权重近似单位矩阵（只保留了对角线）。真实有 `InputMixer3dWeights_514[3]` 等加权。
- `downmix`：12→2 `input_mixer_3d`，只把输入 0/1 直送到 L/R。真实 `Weights_L_R[8]` 会混合多路。
- `bass` / `midrange` / `treble`：Symphony 三段音调控制，默认 0 dB（平直）。
- `balance`：左右平衡，默认 0（中心）。
- `volume`：`gain_ramper`，默认 0 dB。
- `preemp`：2 通道 10 阶 IIR bank，对应 `LevelDetectPreEmpFilterpooliirCoeffs`，目前系数未填。
- `level_detect`：2 通道 RMS 检测，mode=1。输出只接 `ld_sink`（null_sink），用于读取电平探针。
- `buf_router`：12→32 通道路由，前 12 路填充，后 20 路置 -1。
- `buf1_sel`：12→10 选择，取前 10 路。
- `buf2_sel`：12→1 选择，取第 11 路（索引 11）。

**设计要点**：

- 响度/音调/平衡/音量链路只在 2 路立体声上运行，这是 Symphony 预放 Part1 的典型设计。
- `buf_router` 把 12 路扩展成 32 路，是因为后级 `post_process` 的 `asd_router` 期望 32 路输入（再选择 22 路）。
- `buffer1`（10 路）和 `buffer2`（1 路）是 Audiopilot 的“参考信号”和“单声道信号”。

---

### 3.7 `post_process` — 后处理

**真实作用**：PostEQ → ASDRouter（主通道+辅助通道选择） → OutputCalibration/FreqComp → Limiter → SoftClipper → MuteRamper → AudioOut。

**当前实现**：

- `asd_router`：32→22 `channel_router`，取前 22 路。
- `post_eq`：22 通道 13 阶 IIR bank，系数未填。
- `limiter`：22 通道限幅器，threshold=-1 dB，所有通道攻击/释放/系数相同。
- `sclip`：`soft_clipper`，默认 drive=0 dB。
- `mute_ramp`：`gain_ramper`，100ms 斜坡，默认 0 dB。
- `calib`：22→22 `output_router`，identity。
- `freq_comp`：22 通道 13 阶 IIR bank，系数未填。
- `out_delay`：22 通道延迟线，目前全 0。
- `audio_out_router`：22→32 通道路由，填充前 22 路，后 10 路置 -1。
- `audio_sink`：32 路 null_sink，消耗 `audio_out_router` 的输出。

**注意**：`audio_out_router` + `audio_sink` 对应 Symphony 的 `AudioOut[32]` 分支（主 22 路 + 辅助/未用 10 路）。step0 中主输出从 `out_delay:out` 走，而 `audio_out_router` 只是保持结构完整。

---

### 3.8 `audiopilot` — Audiopilot35 噪声补偿侧链（TID0 部分）

**真实作用**：Symphony Audiopilot 算法，根据车内噪声自适应提升音乐响度。完整实现跨 TID0/TID1/TID3/TID4/TID5 多个分速率任务。step0 只实现了 TID0 的正弦调制链，以及几个分析侧链的“抽头”。

**当前实现**：

TID0 信号链：

- `buf_split`：10→10 identity，把 `buffer1` 分配给多个下游。
- `bpf` / `lpf`：10 通道 IIR bank（2 阶/4 阶），系数未填，对应 HF_Bandpass/Lowpass。
- `bpf_gain` / `lpf_gain`：`gain_ramper`，默认 0 dB。
- `bpf_delay`：10 通道延迟线，目前全 0。
- `sum`：把 bpf 和 lpf 两路相加。
- `wide_gain`：最终宽频增益控制，默认 0 dB。
- `sine_mod`：1 Hz 正弦调制器，depth=0（当前无调制）。
- `mod_matrix`：10×10 identity，把正弦调制信号广播到 10 路。
- `pooliir_buf` / `pooliir_sink`：poolIIR 输入缓冲的占位，当前只是 identity → null_sink。

分析侧链抽头（只监控，不反控）：

- `hf_est` → `hf_psd` → `hf_sink`：10 通道相干矩阵 → PSD →  sink，对应 TID3 HF 噪声估计。
- `mic_ld` → `ref_delay` → `ref_sink`：1 通道电平检测 + 延迟，对应 TID1 ref/mic 对齐。
- `mono_ld` → `lf_interp` → `lf_sink`：1 通道电平检测 + 查表插值，对应 TID4 LF 速度界限。
- `fdp_ld` → `coh_power` → `coh_sat` → `coh_sink`：4 通道电平检测 → 平方 → 饱和 → sink，对应 TID5 相干求和。
- `mic_sink` / `mono_sink` / `fdp_sink`：直接消耗对应 `level_detect` 输出。

**为什么大量接 sink**：这些分析节点的输出在真实系统中会通过共享内存/控制参数反控 TID0 的增益，但 Orpheus 当前图模型无法把分析结果作为控制信号送回（无控制端口）。所以先用 `null_sink` 保留抽头位置，等控制协议支持后再连接。

---

## 4. 参数来源说明

本工程的参数大致分为三类：

### 4.1 来自 TOP 文件（后续需回填）

这些是 `Model_1_1_*_TOP.c` 中的调音参数，step0 中要么是默认值，要么全 0/identity：

| 分区 | 文件 | 主要参数 |
|---|---|---|
| `PreAmp_p0_b0` | `Model_1_1_PreAmp_p0_b0_TOP.c` | InputMixer3dWeights、Bass/Midrange/Treble 系数、Balance/Volume 表、PreEmp IIR 系数 |
| `PreAmp_p3_b0` | `Model_1_1_PreAmp_p3_b0_TOP.c` | FdpCoeffs、FdpSpum、DirectPathSamplesDec、TrebleDelay |
| `PreAmp_p5_b0` | `Model_1_1_PreAmp_p5_b0_TOP.c` | MixEqpooliirCoeffs、Cs/Left/Right TargetGains（166 个） |
| `PreAmp_p8_b0` | `Model_1_1_PreAmp_p8_b0_TOP.c` | HoligramIirpooliirCoeffs |
| `PreAmp_p12_b0` | `Model_1_1_PreAmp_p12_b0_TOP.c` | SleepingBeauty 表、ChannelToRamperMap |
| `PreAmp_p13_b0` | `Model_1_1_PreAmp_p13_b0_TOP.c` | FadeControl、FadeRamper、PostHoligramRoutingMap、SpeakerDelay |
| `PreAmp_p14_b0` | `Model_1_1_PreAmp_p14_b0_TOP.c` | Audiopilot 全部系数 |
| `PostProcess_p0_b0` | `Model_1_1_PostProcess_p0_b0_TOP.c` | ASDRouter、OutputEQ、FreqComp、Limiter、SoftClipper |

### 4.2 来自 `Model_1_1.c` 的硬编码常量

- 延迟线长度：`TrebleDelay=1280`、`TrebleSurroundDelay=1664`、`SpeakerDelay` 表、`feedback_delay=32`。
- 通道映射：`feedback_expand` 的 22→30 路由矩阵、`reorder` 的 Selector1 索引、`ts_expand` 的 7→13 映射。
- 限幅器攻击/释放系数：`0.0236999`、`1.00023985`、`0.0118499501`、`0.316227764`。
- `slc_matrix_mul` 默认 `ramp_coeff=0.995842`。

### 4.3 工程占位默认值

- 所有 `iir_bank` 未提供系数时，默认是单位矩阵（直通）。
- 所有 `gain` / `gain_ramper` 默认 0 dB。
- `sine_mod` 频率 1 Hz、depth 0（即无调制）。
- 所有延迟线默认 0 样本延迟（除非明确写了延迟值）。
- `matrix_mul` / `output_router` / `input_mixer_3d` 大量使用 identity 或对角矩阵。

---

## 5. 占位、已知问题与限制

### 5.1 真实 FDP 未实现

`part2_fdp` 目前只是 `delay_line + matrix_mul`，没有 256 点 STFT、混响提取、PSD 平滑和 IFFT 重叠相加。这会导致：

- 环绕声解码完全不对（只是左右声道原样输出）。
- FDP 到 Audiopilot 的 `fdp_lo_ro` 信号没有真实频域信息。

### 5.2 大量系数为 identity/0 dB

Part3/Part4/Part5/Part6/PreAmp/PostProcess/Audiopilot 中的 IIR bank、混音矩阵、延迟表都未回填。当前输出基本等于输入的线性组合，没有真实的调音效果。

### 5.3 多速率 TID 未在图中建模

`model_tree` 描述了 TID0~TID5 的任务分区，但 YAML 图中所有节点都在同一个 1500 Hz 任务里运行。真实系统需要：

- TID1（750 Hz）：ref/mic 延迟缓冲。
- TID2（375 Hz）：FDP FFT 核心。
- TID3（23.4 Hz）：HF 噪声估计（FFT + 相干矩阵 + PSD）。
- TID4（5.86 Hz）：LF 速度界限查表。
- TID5（1.95 Hz）：相干求和。

Orpheus 目前可以通过 `scheduling.divisor` 或 `downrate`/`resample` 组件实现分速率，但 step0 尚未接入。

### 5.4 控制信号无法闭环

Audiopilot 的分析节点（`hf_psd`、`lf_interp`、`coh_sat` 等）目前只能读到探针，无法自动把结果写回 `wide_gain` 或 `volume`。需要 Orpheus 控制协议支持才能把分析输出作为参数注入。

### 5.5 反馈环稳定性

`feedback_delay` 只延迟 32 样本（1 块），`feedback_expand` 把 22 路扩展成 30 路。由于当前增益都是 0 dB/identity，环不会自激；但如果后续回填系数时不注意相位和增益，可能会引入啸叫或不稳定。

### 5.6 测试信号是简单的正弦波

`music_src`（30 路 500 Hz 正弦）、`treble_src`（2 路 8 kHz）、`mono_src`（1 路 250 Hz）、`treble_surround_src`（7 路 4 kHz）。这些只是为了验证链路能跑通，不代表真实车载音频内容。

---

## 6. 下一步工作

1. **回填 TOP 系数**：把 `Model_1_1_*_TOP.c` 中的参数按分区写入对应 `iir_bank` / `slc_matrix_mul` / `delay_line` / `sleeping_beauty` 等组件。
2. **实现真实 FDP**： either 扩展 `part2_fdp` 为更大的子图（RFFT + 频域处理 + IFFT）， either 开发一个专用的 `fdp` 组件。
3. **接入多速率调度**：把 FDP FFT 和 Audiopilot 分析链放到 `downrate` 子图中，按 `model_tree` 的 TID 分区执行。
4. **控制闭环**：让 Audiopilot 分析结果能通过 RT host 的 `SET` 命令或控制端口影响 `wide_gain` / `volume`。
5. **数值一致性验证**：和 `Model_1_1.c` 的参考输出逐样本对比，确保回填后误差在可接受范围。
6. **真实输入替换测试源**：把 `signal_gen` 换成 `wav_in` 或设备输入，做听音验证。

---

## 7. 调试建议

- **先看通不通**：`python -m orpheus_core.cli compile examples/symphony_sas_step0.yaml` 能否成功生成 plan.json。
- **再看通道数**：重点关注 30→12→32→22、6→13→22、22→30 这些转换点，容易因索引或矩阵尺寸出错。
- **探针监控**：`main_probe`（22 路 RMS）和 `ap_probe`（10 路 RMS）是快速判断输出是否有信号的窗口。
- **逐步开关节点**：可以把 `part2_fdp`/`part3_mixing`/`part4_peripheral_eq`/`part5_6` 等子组件临时替换为 passthrough，分段定位问题。
- **检查 WAV 输出**：`symphony_step0_main_out.wav`（22ch）和 `symphony_step0_ap_out.wav`（10ch）可以用 Audacity 等工具查看波形。

---

## 8. 关键文件索引

| 文件 | 作用 |
|---|---|
| `examples/symphony_sas_step0.yaml` | 本工程主文件 |
| `examples/symphony_sas_step0.node-notes.json` | 每个节点实例的简明说明 |
| `components/orpheus/builtin/slc_matrix_mul/README.md` | Part3 核心组件详细说明 |
| `components/orpheus/builtin/sleeping_beauty/README.md` | Part5 SleepingBeauty 说明 |
| `components/orpheus/builtin/input_mixer_3d/README.md` | PreAmp InputMixer3D/Downmix 说明 |
| `docs/symphony_sas_model_1_1_flow.md` | Symphony 整体流程（如有） |
