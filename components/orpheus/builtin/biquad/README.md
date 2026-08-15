# orpheus.builtin.biquad — 双二阶滤波器

## 功能

通用二阶 IIR 滤波器（biquad）：在 `prepare` 时按 RBJ Audio EQ Cookbook 公式，由 `type / fc / q / gain_db` 和采样率算出 5 个系数（b0, b1, b2, a1, a2），`process` 中对每个通道逐样本做二阶递归滤波。

常用场景：高低切、单段参量 EQ、陷波（去电源哼声）、搁架式音色调整。多个 biquad 串联即可搭出多段 EQ；需要 2 段 peaking 且支持运行时直写系数时，可用聚合组件 `orpheus.builtin.biquad_bank`。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | 输入音频，`channels` 通道 |
| out | output | audio | 滤波后的音频，通道数与输入相同 |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `type` | string | lowpass | 滤波器类型（7 种，见下表）；修改后需重启（restart_required） |
| `fc` | float | 1000.0 Hz | 截止/中心频率，20~20000 Hz |
| `q` | float | 0.707 | 品质因数，0.1~10.0 |
| `gain_db` | float | 0.0 dB | 增益，-24~+24 dB（仅 peaking / lowshelf / highshelf 使用） |
| `channels` | int | 2 | 通道数，1~32；改变后需重新编译（affects_signature） |
| `form` | string | df2t | 滤波结构：`df2t`=DF-II 转置（滚动延迟单元，寄存器最少、数值性质好，推荐）；`df1`=传统直接 I 型（教学/对照用）。两者传递函数相同 |

## 关键参数详解

### `type`：七种滤波形状

| 值 | 中文名 | 直觉含义 |
|---|---|---|
| `lowpass` | 低通 | 保留 fc 以下、衰减以上。最常用的高低切。 |
| `highpass` | 高通 | 保留 fc 以上、衰减以下。去低频轰隆/直流。 |
| `bandpass` | 带通 | 只保留 fc 附近的频带，两侧衰减（恒定裙边增益形态，峰值处增益为 1）。 |
| `notch` | 陷波 | 在 fc 处挖一个深坑，其余基本不动。典型用途：消除 50/60 Hz 电源哼声。 |
| `peaking` | 峰值 | 在 fc 处抬升/压下一个"铃铛"，幅度由 `gain_db` 决定。参量 EQ 的基本单元。 |
| `lowshelf` | 低频搁架 | fc 以下整体抬升/压下 `gain_db`，像把低频地板整体抬高/降低。 |
| `highshelf` | 高频搁架 | fc 以上整体抬升/压下 `gain_db`。 |

系数公式为标准 RBJ Cookbook：`A = 10^(gain_db/40)`，`w0 = 2π·fc/fs`，`alpha = sin(w0)/(2q)`，各类型按对应公式生成并统一除以 a0 归一化。未知的 `type` 字符串会退化为 lowpass。

### `q`：带宽的倒数直觉

Q 控制"过渡带/谐振峰有多窄"：

- **Q = 0.707（1/√2）**：Butterworth  maximally flat 响应，低通/高通的最平坦默认选择。
- **Q 越大**：峰/坑越窄越尖，谐振感越强；peaking 时影响的频带越窄；低通时 fc 附近会出现隆起（共鸣感）。
- **Q 越小**：过渡越平缓，作用范围越宽。

对 peaking/notch，带宽（Hz）≈ fc / Q 可作为粗略直觉。

### `gain_db` 只作用于三种类型

`lowpass / highpass / bandpass / notch` 完全忽略 `gain_db`（公式里不出现 A）。只有 `peaking / lowshelf / highshelf` 使用它。正值提升、负值衰减。

## 注意事项

- 全部参数都是 `restart_required`：运行中不能实时改频率/Q/增益，修改后需要重启（重新 prepare 才会重算系数）。需要运行时平滑调参的 EQ 请用 `biquad_bank` + BULK 直写系数，或增益类组件（如 `gain_ramper`）。
- 系数在 `prepare` 时按**当前采样率**计算；同一份工程换采样率部署会自动重算，无需手动干预。
- 系数计算用到 `powf/sinf/cosf/sqrtf`，但只发生在 `prepare`，不在实时路径。
- 本组件 v1.1.0 起提供两种差分结构（`form` 参数）：DF-II 转置（默认，`y = b0·x + z1`，滚动更新 z1/z2）与传统 DF-I（`y = b0·x + b1·x₁ + b2·x₂ − a1·y₁ − a2·y₂`）。两者传递函数完全相同，仅寄存器用量与 float 舍入路径不同；脉冲响应已与 RBJ float64 参考逐样本对齐（测试 `test_biquad_forms.py`）。v1.0.0 的递推曾误用输出历史充当输入历史，频响偏离设计值（fc 处可达 -13dB），已修复。

## 典型用法

```yaml
- id: lp
  component: orpheus.builtin.biquad
  params: { type: lowpass, fc: 8000, q: 0.707, channels: 2 }

- id: hum_notch
  component: orpheus.builtin.biquad
  params: { type: notch, fc: 50, q: 8, channels: 2 }

- id: presence
  component: orpheus.builtin.biquad
  params: { type: peaking, fc: 3000, q: 1.0, gain_db: 4.0, channels: 2 }
```

## 实时安全

- `process` 无内存分配、无锁、无 IO、无三角函数（系数已在 prepare 算好），支持就地处理（supports_inplace）。
- 状态为每通道 4 个历史单元（DF-I 用 x1/x2/y1/y2，DF-II 转置用 z1/z2），`reset` 只清零历史，不重算系数。
