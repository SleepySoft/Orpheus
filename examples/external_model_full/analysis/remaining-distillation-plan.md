# 剩余蒸馏与控制流计划

> 更新：2026-10-13
>
> 基线：67 definitions / 51 leaves / 14 verified / 37 golden blockers。

## 1. 剩余叶子分类

### 1.1 现有通用组件可组合（11）

- `A.SPATIAL_FULL.FADE_SUM`
- `A.SPATIAL_FULL.MIX.ORGANIZE`
- `A.SPATIAL_FULL.PERIPHERAL_EQ`
- `A.AUDIOPILOT.HF_PREPROCESS`
- `B.BANDS.SRC`
- `B.BANDS.MONO_DETECTOR`
- `B.HEQ.PERIPHERAL`
- `B.HEQ.HEADREST`
- `B.HEQ.OVERHEAD`
- `B.POST.ROW1.XTALK`
- `B.POST.ROW2.XTALK`

优先顺序：边界与参数已明确的路由/HEQ/Xtalk 先做；每项仍需默认、非零、跨块、reset/bypass golden，不能因组件已存在直接标 verified。

### 1.2 需要新增可复用算法原语（14）

- A FDP：`ANALYSIS`、`COEFFICIENTS`、`REVERB`、`SYNTHESIS`
- B FDP：`STFT`、`COEFFICIENTS`、`REVERB`、`WOLA`
- Audiopilot：`HF_ESTIMATOR`、`LF`
- Bands：`B.BANDS.DECAY`
- PreAmp：`A.PREAMP.ROUTE_LEVEL` 需要 block-rate 路由表与 RMS/hold/decay tracker
- PostProcess：`A.POSTPROCESS.PROTECTED` 需要 samplewise ramp-bank 与 source-recursive limiter
- PostProcess：`A.POSTPROCESS.ROUTER_OUTPUT` 需要 71→22 main+aux 数组路由与 samplewise AllMute

共享原语按依赖实现：STFT/WOLA transport → 复谱系数/平滑 → spectral history/冲击分离 → covariance/min-stat/onset tracker。A/B FDP 必须复用相同原语，参数与状态不同由实例配置表达。

### 1.3 必须先修边界/时序（5）

- `A.SPATIAL_FULL.FDP.TRANSPORT`
- `B.FDP.BUFFER`
- `B.MIX_DECI.ALIGN`
- `B.POST.RETURN`
- `B.MIX_CONTROL.SEQUENCE`

需要确认：跨 TID ring 初始相位、同 tick 调用序、B3/B4 frame shape、13/3 return 的读写相位、sequence 奇偶/回绕/reset。

### 1.4 依赖数组控制（7）

- `A.AUDIOPILOT.GAIN_APPLICATION`
- `A.SPATIAL_FULL.MIX.RAMP_CONTROL`
- `B.MIX_CONTROL.FEEDBACK`
- `B.MIX_CONTROL.TARGETS`
- `B.MIX_DECI.RAMP`
- `B.POST.ROW1.GENERATION`
- `B.POST.ROW2.GENERATION`

框架已实现同 Task 标准 BULK `control_connections`：显式 `kind: bulk` + `bulk_slot`、两相静态快照、目标 double bank、动态/生成双路径一致。跨 Task/Runtime 仍禁止隐式连接，必须经过显式事务桥。

### 1.5 硬件数值证据 blocker（1）

- PostProcess PoolIIR：limiter 递推已从生成源码恢复；真正 blocker 是 PostEQ/FreqComp 非平凡 PoolIIR accelerator 布局、舍入与目标 goldens。详见 `analysis/a-route-level-postprocess-audit.md`。

## 2. 已确认控制流

Algorithm IR 的 `relations` 记录以下事实：

| ID | 从 | 到 | shape | 当前 lowering |
|---|---|---|---|---|
| `ctl_a_hf_metrics` | HF_ESTIMATOR.metrics | GAIN_APPLICATION.hf_metrics | [4] | 跨 Task，待控制桥 |
| `ctl_a_lf_metrics` | LF.metrics | GAIN_APPLICATION.lf_metrics | [4] | 跨 Task，待控制桥 |
| `ctl_b_deci_targets` | MIX_CONTROL.deci_targets | MIX_DECI.targets | [1134] | B3 同 Task BULK，叶子实现后提升 |
| `ctl_b_vls_row1/2` | MIX_CONTROL.vls_row* | POST.ROW*.targets | [40] | B3 同 Task BULK，叶子实现后提升 |
| `bus_ctl_a_current_to_b` | A MIX current | B MIX_CONTROL current | [166] | A→B 事务桥 |
| `bus_ctl_a_fade_to_b` | A FADE targets | B MIX_CONTROL fade | [2] | A→B 事务桥 |
| `bus_ctl_b_targets_to_a` | B targets | A MIX targets | [166] | B→A 事务桥 |
| `bus_ctl_b_decay_to_a` | B decay | A FDP reverb.decay | [1] | B1→A2 事务桥 |
| `bus_evt_b_sequence_to_a` | B sequence | A MIX sequence | event u32 | 与 target 同事务 |

`B.MIX_DECI.RAMP.active → B.MIX_CONTROL.FEEDBACK.active` 仍只是源码语义候选；生产/快照时机未完成双向确认，暂不写 relation。

## 3. 控制 lowering 规则

1. 同 Task 且两端已实现公开 BULK 参数：提升为标准 `control_connections`。
2. 参数必须显式 `kind: bulk`、声明 shape 和 `bulk_slot`；slot 容量不得小于 shape count。
3. 目标 slot 必须 double bank；块末尾 stage，下一块开始前原子 commit，每条链固定一块延迟。
4. 跨 Task/Runtime 控制直接编译拒绝；使用显式 bridge，携带 sequence/epoch/validity。
5. event 不降格为 float control；fast sequence 与三组 target 必须同一事务发布。
6. spectrum/frame 数据仍走计算数据流，不走参数控制链。

## 4. 实施阶段

1. **边界阶段**：host phase、bus atomicity/latency、sequence、B3/B4/return ring。
2. **现有组件阶段**：13 个可组合叶子，逐项切片+golden。
3. **频域原语阶段**：共享 STFT/WOLA、谱系数、history、估计器。
4. **数组控制叶子阶段**：先 B3 同 Task 三条 target 流，再 A/B 跨 Task/Runtime bridge。
5. **系统闭环阶段**：消除 37 个 blocker，动态/生成与参考实现逐 Task 对拍。

## 5. 停止条件

- 全部叶子六门禁 verified；
- `coverage-report.json` golden blocker 为 0；
- 完整图无 placeholder，可编译；
- 音频只走标准音频边，参数只走标准控制边；
- 跨 Task/Runtime 事务有显式 bridge，sequence/epoch 与音频/数组不撕裂；
- 动态 Runtime、生成 C 和参考实现通过约定 golden。
