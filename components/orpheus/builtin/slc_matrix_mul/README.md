# slc_matrix_mul - N 表插值斜坡混音矩阵

> Orpheus 专用组件，对应 Symphony SAS Symphony Part3 MixingControl + RampProcessing 的一体化实现。

## 功能

将 **N 表插值**、**一阶 IIR 斜坡渐变**、**矩阵乘法**三个紧密耦合的操作封装在单个组件内，逐样本执行。

核心解决的问题：用户控制信号（如环绕声等级 `surround_index`）需要在多张预调增益表之间插值出目标矩阵，再经 IIR 平滑过渡后用于音频混音。这三步在同一个样本循环内交替执行（先更新增益、再用更新后的增益做矩阵乘），无法拆分为多个组件。

## 设计背景

### 为什么不能拆分

Symphony 的 Part3 混音使用三组斜坡渐变混音矩阵（Cs 13x2、Left 7x10、Right 7x10），每组有 Min/Detent/Max 三张预调表。完整处理链路：

```
surround_index(0-255, 用户控制)
    -> N表插值(Min/Detent/Max, 线性/对数线性)  -> target[166]
    -> 一阶IIR渐变(active = c*active + (1-c)*target, FastSequence冻结/释放)
    -> 矩阵乘(out = active * in)
```

拆分为多组件不可行的原因：

1. **逐样本耦合**：IIR 渐变和矩阵乘在同一个 sample 循环内交替执行。Orpheus 组件间按块（32样本）通信，无法实现逐样本的数据传递。
2. **无连续 control-rate 端口**：Orpheus 已支持参数级 `control_connections`（块边界两相快照，每链 1 块延迟），但尚未实现逐样本 control-rate 缓冲端口。166 个矩阵系数也不能在每个样本间通过参数链重写。
3. **时序与数据量不匹配**：`interp_index` 是块边界标量控制值，矩阵系数是 bulk 数组；即使参数控制链可用，也不能替代同一 sample loop 内的插值、IIR 渐变和矩阵乘。

因此仍需一体化组件。`interp_index` 可从图外通过 `SET` 注入，也可在 manifest 声明为 bindable 后由参数控制链在块边界驱动；插值/渐变/乘法继续在 `process` 内逐样本完成。

## 端口

| 端口 | 方向 | 类型 | 通道 |
|------|------|------|------|
| `in` | 输入 | audio f32 | param:cols |
| `out` | 输出 | audio f32 | param:rows |

输入输出通道数可独立配置（rows != cols 时实现上混/下混）。

## 参数

| 参数 | 类型 | 默认 | 范围 | 更新策略 | 说明 |
|------|------|------|------|----------|------|
| `rows` | int | 2 | [1, 32] | restart | 输出通道数（影响签名） |
| `cols` | int | 2 | [1, 32] | restart | 输入通道数（影响签名） |
| `num_tables` | int | 1 | [1, 8] | restart | 插值表数量 N |
| `tables` | string | "1,0,0,1" | - | restart | N x rows x cols 个浮点，表优先行优先 |
| `interp_x` | string | "0" | - | restart | N 个插值断点 x 轴值 |
| `interp_index` | float | 0.0 | - | **immediate** | 插值位置（运行时 SET 可改） |
| `interp_method` | int | 0 | [0, 1] | restart | 0=线性, 1=对数线性 |
| `ramp_coeff` | float | 0.0 | [0, 1] | restart | IIR 系数 c（0=瞬切, ~1=慢渐变） |
| `freeze` | int | 0 | [0, 1] | **immediate** | 0=渐变, 1=冻结（运行时 SET 可改） |

### 运行时可调参数

`interp_index` 和 `freeze` 为 `immediate` 更新策略，可通过实时协议在运行时修改：

```
SET <node_id> interp_index 128.0
SET <node_id> freeze 1
```

其余参数为 `restart_required`，修改后需重新编译/加载。

## 算法

### 1. N 表插值（compute_target）

当 `interp_index` 变化时（`targetDirty` 标志置位），在 `process` 开头重算目标矩阵：

```
若 N=1: target = tables[0]    // 无插值

若 N>=2:
  查找 interp_index 所在分段 [i, i+1)
  t = (interp_index - interp_x[i]) / (interp_x[i+1] - interp_x[i])  // 分数位置

  线性:      target[k] = (1-t) * table_i[k] + t * table_{i+1}[k]
  对数线性:  target[k] = exp((1-t)*ln(table_i[k]) + t*ln(table_{i+1}[k]))
```

边界处理：
- `interp_index <= interp_x[0]`: target = tables[0]
- `interp_index >= interp_x[N-1]`: target = tables[N-1]
- 对数线性模式下，增益值钳位到 `5.01e-7`（-126dB）防止 `log(0)`

### 2. 一阶 IIR 斜坡渐变（逐样本）

每个音频样本先更新 active 矩阵，再用更新后的 active 做矩阵乘：

```
若 freeze=1:  active 不变（冻结）
若 freeze=0:
  若 ramp_coeff <= 1e-6:  active = target（瞬切）
  否则:  active[k] = ramp_coeff * active[k] + (1-ramp_coeff) * target[k]
```

- `ramp_coeff = 0`: 瞬切，无渐变（适合静态配置）
- `ramp_coeff = 0.995842`: Symphony 默认，tau ~= 238 样本 ~= 5ms @ 48kHz
- `ramp_coeff = 1`: 永不收敛（实际不会使用）

### 3. 矩阵乘

```
out[frame, r] = sum(active[r*cols + c] * in[frame, c])  for c in 0..cols-1
```

标准矩阵-向量乘，每帧每输出通道一次。

### 完整 process 伪代码

```
process(frames, in, out):
    if targetDirty:
        compute_target()       // N表插值
        targetDirty = 0

    for f in 0..frames-1:
        if not freeze:
            if ramp_coeff <= epsilon:
                active = target            // memcpy
            else:
                for k in 0..elem-1:
                    active[k] = c*active[k] + (1-c)*target[k]

        for r in 0..rows-1:               // 矩阵乘
            acc = 0
            for c in 0..cols-1:
                acc += active[r*cols+c] * in[f*cols+c]
            out[f*rows+r] = acc
```

## 使用方式

### 基本示例（3 表插值，2x2 矩阵）

```yaml
- id: mixer
  component: orpheus.builtin.slc_matrix_mul
  task: default
  params:
    rows: 2
    cols: 2
    num_tables: 3
    # 3张表，每张 2x2=4 个值，表优先：
    # Table 0 (Min,    interp_x=0):   [1, 0, 0, 1]   单位矩阵
    # Table 1 (Detent, interp_x=128): [0.7, 0.3, 0.3, 0.7]  半混
    # Table 2 (Max,    interp_x=255): [0.5, 0.5, 0.5, 0.5]  全混
    tables: "1, 0, 0, 1, 0.7, 0.3, 0.3, 0.7, 0.5, 0.5, 0.5, 0.5"
    interp_x: "0, 128, 255"
    interp_index: 0.0       # 初始用 Table 0
    interp_method: 0         # 线性插值
    ramp_coeff: 0.995842     # ~5ms 渐变
    freeze: 0
```

### Symphony Part3 Cs 混音（13x2，3 表）

```yaml
- id: cs_mixer
  component: orpheus.builtin.slc_matrix_mul
  task: default
  params:
    rows: 2
    cols: 13
    num_tables: 3
    # 3 x 26 = 78 个增益值（调音师在实车调定的 Min/Detent/Max）
    tables: "<78个浮点值，逗号分隔>"
    interp_x: "0, 128, 255"
    interp_index: 128.0      # 出厂默认 Detent
    interp_method: 1         # 对数线性（听感均匀）
    ramp_coeff: 0.995842     # Symphony 默认 IIR 系数
    freeze: 0
```

### 运行时控制（rt_host 协议）

```
SET cs_mixer interp_index 200    # 用户调高环绕等级
SET cs_mixer freeze 1            # 模式切换中途冻结
SET cs_mixer freeze 0            # 解冻，开始渐变到新 target
```

### 单表模式（N=1，退化为静态 matrix_mul）

```yaml
- id: static_mixer
  component: orpheus.builtin.slc_matrix_mul
  params:
    rows: 2
    cols: 2
    num_tables: 1
    tables: "0.5, 0.5, 0.5, 0.5"
    ramp_coeff: 0.0              # 无渐变
```

N=1 时无插值，target 始终等于 tables[0]，行为等价于 `matrix_mul`（但多了 active/target 冗余）。

## tables 参数格式

`tables` 是逗号分隔的浮点字符串，布局为**表优先、行优先**：

```
[table0_row0_col0, table0_row0_col1, ..., table0_rowR-1_colC-1,
 table1_row0_col0, ...,
 tableN-1_row0_col0, ..., tableN-1_rowR-1_colC-1]
```

总长度 = `num_tables * rows * cols`。

`interp_x` 是逗号分隔的 N 个断点值，需单调递增。若提供的值不足 N 个，自动均分 [0, 1]。

## 限制

1. **最大矩阵尺寸**：32x32 = 1024 元素（`SLC_MM_MAX_ELEM`）。超过会截断。
2. **最大表数量**：8（`SLC_MM_MAX_TABLES`）。
3. **内存占用**：state 固定分配 `8 * 1024 * 3 * sizeof(float)` ~= 96KB（tables + target + active），无论实际使用多少。
4. **tables 不可运行时更新**：`restart_required` 策略。调音表内容在 `prepare` 时解析，运行时只能改 `interp_index`（在表间插值）和 `freeze`。若需运行时换表，需重新编译工程。
5. **无 downmix 混合**：Symphony 的 `targetGain = (1-downmix)*slcGain + downmix*fadeDownmixGain` 未实现。当前 target 直接来自插值，不与 fade downmix 混合。如需此功能，可在上游用 `mixer` 组件预混合，或将 downmix 逻辑加入此组件。
6. **无 FastSequence 序列号**：Symphony 用奇偶序列号控制冻结/释放，本组件用 `freeze` 布尔参数（0/1）替代，语义等价但无序列号防抖。
7. **对数线性模式**：增益值为负或零时钳位到 `5.01e-7`（-126dB），不会产生 NaN，但负增益的物理含义（反相）在此模式下丢失。
8. **不支持 inplace**：输入输出缓冲区不能重叠（矩阵维度变化时数据布局不同）。

## 与 Symphony 的对应关系

| Symphony (Model_1_1.c / Model_1_2.c) | slc_matrix_mul |
|--------------------------------------|----------------|
| `SymphonyPart3MixingControl_*TargetGains[26/70/70]` | `tables` 参数（3 张表） |
| `surround_index` (0-255, RTC SET) | `interp_index`（immediate, SET 注入） |
| `InterpolationMethod` (线性/对数线性) | `interp_method` (0/1) |
| `activeCoeffs = 0.995842` (RampCoeff) | `ramp_coeff` |
| `FastSequence` (奇=冻结, 偶=渐变) | `freeze` (1=冻结, 0=渐变) |
| `RampProcessing` 一阶 IIR (`:12681`) | process 内逐样本 IIR |
| `AudioOut = AudioIn * activeGains'` (`:12689`) | process 内矩阵乘 |
| PingPong 跨核共享内存 (N00S1_2_D1_1_F3) | `SET` 命令注入（图外控制） |

### 未覆盖的 Symphony 特性

- **SLC 三表分别为 Cs/Left/Right 独立调音**：Symphony 三组矩阵各有独立的三表。本组件每次实例只处理一组矩阵，需实例化 3 个 `slc_matrix_mul` 分别对应 Cs/Left/Right。
- **DeciRate 计算 + FullRate 执行的多速率分离**：Symphony 在 Model_1_2（1500Hz）算 target，经 PingPong 传到 Model_1_1（48kHz）执行 IIR+乘法。本组件将插值和执行合并在同一速率，无需 PingPong。若需多速率分离，可用 `downrate` + `embed_in/out` 拆分控制路径。
- **CAE（Cabin Acoustic Enhancement）**：Symphony 的 CAE 可替换 tail 权重，本组件未实现。