# orpheus.builtin.level_detect — 电平检测

## 功能

峰值或 RMS 包络检测器：分析输入音频的电平，通过 `level` 探针实时上报，同时把音频原样直通给下游。常用于动态范围处理、自动增益控制（AGC）、VU/峰值表、或把电平反馈给其他控制组件。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `mode` | int | 0 | 检测模式：`0` = 峰值包络，`1` = 块 RMS。 |
| `attack_ms` | float | 10.0 ms | 包络上升时间。 |
| `release_ms` | float | 100.0 ms | 包络下降时间。 |
| `channels` | int | 2 | 通道数，改变后需重新编译。 |
| `level` | float | 0.0 | 当前检测到的最大电平（readback，只读）。 |

### `mode` 的真实含义

- **mode = 0（峰值）**：逐样本取绝对值，然后用 attack/release 系数跟踪包络。反应最快，适合限幅器侧链或峰值表。
- **mode = 1（RMS）**：每个块先算各通道 RMS（均方根），再对 RMS 序列做 attack 平滑。更接近人耳响度感知，适合 VU 表或 AGC。

### `level` 上报的是什么

`level` 是所有通道包络值中的最大值，范围 0~∞（通常 0~1 为正常，>1 表示削波）。它不等于 dB，如需 dB 需要在读取后做 `20*log10(level)`。

### attack/release 的物理意义

内部是一阶指数平滑：

- 当瞬时电平高于当前包络时，用 `attack_ms` 向上跟踪。
- 当瞬时电平低于当前包络时，用 `release_ms` 向下跟踪。

`attack_ms` 越小，包络对突发的响应越快；`release_ms` 越大，包络保持时间越长。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，与输入完全一致。

## 注意事项

- `mode` 是 `restart_required`，运行时不能切换。需要两种模式同时存在时，请并排放两个 `level_detect`。
- `level` 只反映当前块结束时的状态；如果块长很大，电平更新频率会降低。
- 实时安全，process 内无三角函数、无内存分配。

## 典型用法

```
source ──► level_detect(mode=1, attack_ms=10, release_ms=100)
       ├──► out
       └──► (level 探针 → UI 电平表)
```

把 `level_detect` 放在主链路中，即可在 UI 实时观察信号电平，同时不影响音频本身。
