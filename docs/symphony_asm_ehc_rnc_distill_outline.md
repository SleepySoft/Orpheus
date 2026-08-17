# ASM（EHC + RNC）蒸馏提纲

> 源工程：`C:\D\Work\Project\EREV\cart-cicd-erev-asm\components\symphony`
> 目标：在 Orpheus 中建立 `examples/symphony_asm_ehc_rnc.yaml` step0 骨架，拓扑/任务/参数分区与源模型对齐，算法先用占位组件近似。

---

## 1. 源模型关键事实

### 1.1 顶层 I/O 与任务率

| 端口 | 方向 | 采样率 | 调用率 | 块长 | 通道数 | TID |
|---|---|---|---|---|---|---|
| `Model_AsmIn` | 输入 | 48 kHz | 2 kHz | 24 | 25 | 1 |
| `Model_AudioIn` | 输入 | 48 kHz | 1.5 kHz | 32 | 22 | 2 |
| `Model_AudioOut32` | 输出 | 48 kHz | 2 kHz | 24 | 24 | 1 |
| `Model_Ref_out` | 输出 | 48 kHz | 1.5 kHz | 32 | 18 | 2 |

7 个同步任务：

| TID | 周期 | 速率 | 主要职责 |
|---|---|---|---|
| 0 | 0.1667 ms | 6 kHz | 最快基础步 |
| 1 | 0.5 ms | 2 kHz | EHC 主链、RNC 主链、AudioOut32 |
| 2 | 0.6667 ms | 1.5 kHz | AudioIn / Ref_out |
| 3 | 4 ms | 250 Hz | EHC 慢速控制 |
| 4 | 5 ms | 200 Hz | RNC 慢速控制 |
| 5 | 32 ms | 31.25 Hz | RNC 状态机 / 发散检测 |
| 6 | 128 ms | 7.8125 Hz | RNC 噪声底 / 长期监控 |

### 1.2 顶层子系统

```
Model_Target
├── AsmBlock
│   ├── InputSelect          # 兼容哈希与路由配置
│   ├── EhcSub               # Engine Harmonic Cancellation
│   ├── RncSub               # Road Noise Cancellation
│   └── SignalSplitter       # 信号分配到 EHC/RNC
├── ReferenceCh
├── Output Processing
│   ├── OutputRouter         # 可调延迟 + 通道路由
│   └── RncEntCombine        # RNC 反噪声 + 娱乐音混合
├── OutAggregate
└── TspCaptureControl
```

### 1.3 EHC 内部关键模块

- `AutoStabilizer1p4`：逐谐波幅度/AROC 监控、失真检查、参数修正、训练表。
- `AutoEnhancer2p2`：RPM-ROC 增益增强。
- `Blade`：窄带误差麦克风处理。
- `Core`：谐波振荡器 / FxLMS 类核心控制环。
- `CoreParameters`：频率查找、投影表 W1/W2/W3/W4。
- `Emode` / `Rmode`：发动机模式使能逻辑。
- `Harmonics`：阶次跟踪（可见 2 个谐波）。
- `MicConditioning`：麦克风滤波、饱和、校准。

### 1.4 RNC 内部关键模块

- `InputProcessing`
  - `ActiveSensors`：最多 12 个加速度计、6 个车顶麦克风、8 个扬声器映射。
  - `Downsample`：加速度计与麦克风信号降采样。
  - `FdRoutersControlSignals`：车辆状态路由。
- `RncAlgo`
  - `ControlFilter`：对参考加速度计施加自适应 FIR 系数。
  - `NlmsFullBandTimeDomain/AdaptFilter`：NLMS 系数更新。
  - `SmartSaturation`：输出限幅。
  - `StateMachine`：加速度监控、麦克风监控、音频监控、噪声底检测、发散检测、动态 headroom、Ehx 去相关、车辆控制信号监控。
- `OutputProcessing`：上采样 + 重构滤波，RNC 反噪声与娱乐音合并。

### 1.5 调参分区（TOP）

| TOP 块 | 内容 |
|---|---|
| `Model_Target_Ehc_p0_b0` | 开关、谐波使能、麦克风/扬声器校准增益、FD 路由表、覆盖开关、Blade/Core 参数 |
| `Model_Target_Ehc_p0_b1` | Core Hmu / leakage 频率查找表 |
| `Model_Target_Ehc_p0_b2` | Core 投影表 W3/W4 |
| `Model_Target_Ehc_p0_b3` | Core 投影表 W1/W2 |
| `Model_Target_Ehc_p0_b4` | AutoStabilizer 训练/监控阈值 |
| `Model_Target_Rnc_p15_b0` | 传感器映射、加速度计/麦克风校准、FD 路由、开关、直通 |
| `Model_Target_Rnc_p15_b1` | 各监控器阈值/增益 |
| `Model_Target_Rnc_p15_b2` | NLMS 步长、收敛/发散计数器、智能饱和参数 |
| `Model_Target_Rnc_p15_b3` | NLMS 扬声器-扬声器 Wiener 滤波系数 |
| `Model_Target_Rnc_p15_b4` | NLMS 麦克风-扬声器 Wiener 滤波系数 |
| `Model_Target_Rnc_p15_b5` | NLMS 自适应滤波初始系数 |
| `Model_Target_RncInputSelect_p9_b6` | 保留调参变量 |
| `Model_Target_Sys_p2_b0` | EHC/ENT 混合增益、RncCombine 增益/软削波、输出延迟 |

---

## 2. Orpheus step0 目标

和 `symphony_sas_step0.yaml` 一样，本阶段目标是**结构对齐、可编译、可运行**，而不是“听起来对”。

1. **建立顶层图**：`asm_in` / `audio_in` → `ehc_sub` / `rnc_sub` → `output_processing` → `audio_out` / `ref_out`。
2. **定义 7 个任务**（TID0~TID6），与源模型周期/速率对齐。
3. **用子组件近似 EHC/RNC**：
   - EHC：拆分为 `ehc_input_conditioning`、`ehc_core`、`ehc_blade`、`ehc_output` 等子组件；核心 FxLMS/谐波振荡器先用 `sine_mod` + `gain` + `matrix_mul` + `delay` 占位。
   - RNC：拆分为 `rnc_input_processing`、`rnc_nlms`、`rnc_control_filter`、`rnc_output` 等子组件；NLMS 先用 `fir`/`iir_bank` + `gain` 占位。
4. **保留参数分区元数据**：在 `model_tree.parameter_partitions` 中列出所有 TOP 块，便于后续回填。
5. **输出处理**：`output_router` + `rnc_ent_combiner` 用 `channel_router` / `output_router` / `mixer` 实现。

---

## 3. 模型文件结构

```yaml
version: "0.1.0"
metadata:
  name: Symphony ASM EHC/RNC step0
  description: ...
sample_rate: 48000
block_size: 24          # TID1 块长
target: auto

tasks:
  - id: tid0
    name: TID0 6kHz
    sample_rate: 48000
    block_size: 8
  - id: tid1
    name: TID1 2kHz
    sample_rate: 48000
    block_size: 24
  - id: tid2
    name: TID2 1.5kHz
    sample_rate: 48000
    block_size: 32
  - id: tid3
    name: TID3 250Hz
    sample_rate: 48000
    block_size: 192
  - id: tid4
    name: TID4 200Hz
    sample_rate: 48000
    block_size: 240
  - id: tid5
    name: TID5 31.25Hz
    sample_rate: 48000
    block_size: 1536
  - id: tid6
    name: TID6 7.8125Hz
    sample_rate: 48000
    block_size: 6144

graph:
  nodes:
    - id: asm_in
      component: orpheus.builtin.signal_gen   # 25ch 占位源
      task: tid1
      params: { channels: 25, ... }
    - id: audio_in
      component: orpheus.builtin.signal_gen   # 22ch 占位源
      task: tid2
      params: { channels: 22, ... }
    - id: ehc_sub
      component: sub:ehc_sub
      task: tid1
    - id: rnc_sub
      component: sub:rnc_sub
      task: tid1
    - id: output_processing
      component: sub:output_processing
      task: tid1
    - id: audio_out
      component: orpheus.builtin.wav_out
      task: tid1
      params: { channels: 24, file_path: outputs/symphony_asm_audio_out.wav }
    - id: ref_out
      component: orpheus.builtin.wav_out
      task: tid2
      params: { channels: 18, file_path: outputs/symphony_asm_ref_out.wav }
  connections:
    - asm_in:out -> ehc_sub:asm_in
    - audio_in:out -> rnc_sub:audio_in
    - ehc_sub:out -> output_processing:ehc_in
    - rnc_sub:out -> output_processing:rnc_in
    - output_processing:audio_out -> audio_out:in
    - output_processing:ref_out -> ref_out:in

subcomponents:
  - id: ehc_sub
    name: EHC 子系统
    description: Engine Harmonic Cancellation step0 占位
    ports:
      - { id: asm_in, direction: input, maps_to: ... }
      - { id: out, direction: output, maps_to: ... }
    graph: { ... }
  - id: rnc_sub
    ...
  - id: output_processing
    ...

model_tree:
  base_rate: 6000
  task_flows:
    - tid: 0
      rate: 6000
      block_size: 8
      chains: [...]
    ...
  parameter_partitions:
    - name: Model_Target_Ehc_p0_b0
      ...
```

---

## 4. 关键设计决策

### 4.1 多速率如何处理

源模型有 7 个 TID，Orpheus 当前用 `task` 字段把节点绑定到不同任务。任务间连线需要 rate-bridge 组件。step0 中：

- `asm_in` / `audio_out` 跑在 TID1（2 kHz）。
- `audio_in` / `ref_out` 跑在 TID2（1.5 kHz）。
- EHC 主链 TID1，慢速控制 TID3。
- RNC 主链 TID1，慢速控制 TID4/TID5/TID6。
- 任务间显式用 `downrate` / `resample` / `gain`（保持）占位，而不是自动桥接。

### 4.2 占位策略

| 源模块 | Orpheus step0 占位 |
|---|---|
| EHC Core 谐波振荡器 + FxLMS | `sine_mod` + `gain` + `mixer` |
| EHC Blade 窄带处理 | `iir_bank` + `gain` |
| EHC AutoStabilizer | `gain` + `probe_rms`（监控支路）|
| RNC Downsample | `downrate` + `iir_bank`（抗混叠占位）|
| RNC NLMS 自适应 | `gain` + `mixer` + `probe_rms` |
| RNC ControlFilter | `fir` / `iir_bank` + `matrix_mul` |
| RNC SmartSaturation | `limiter` + `soft_clipper` |
| RNC StateMachine | 多个 `level_detect` + `null_sink` |

### 4.3 输入源

源模型输入是 FD-router 打包的 25 路 `Model_AsmIn` 和 22 路 `Model_AudioIn`。step0 先用 `signal_gen` 生成多通道正弦/噪声占位，后续替换为 `wav_in` / `device_in`。

### 4.4 输出 sink

`audio_out` 24 路 WAV，`ref_out` 18 路 WAV，用于离线验证。

---

## 5. 实施顺序

1. 按本提纲编写 `examples/symphony_asm_ehc_rnc.yaml`。
2. 用 `python -m orpheus_core.cli compile examples/symphony_asm_ehc_rnc.yaml` 验证图可编译。
3. 用 `python -m pytest orpheus_core/tests/ -q` 做回归测试。
4. 可选：在 UI 中导入查看拓扑。
5. 后续：从 `Model_Target_*_TOP.c` 回填系数，逐步实现真实 EHC/RNC 算法组件。

---

## 6. 风险与假设

- 源模型是单核生成 C 代码，未提供 Simulink 图，部分内部连线只能按函数名和调用关系推测。
- 通道语义（哪个通道对应哪个物理麦克风/扬声器）尚未完全明确，step0 用计数占位。
- RNC 降采样因子未从代码中提取，step0 假设 48 kHz → 1.5 kHz（32 倍），后续需核对。
- 任务间 buffer 桥接在 Orpheus 中尚未大规模验证，需测试。
