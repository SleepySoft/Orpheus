# orpheus.builtin.limiter — 峰值限幅

## 功能

峰值限幅器：当信号幅度超过设定阈值时，按包络动态降低增益，防止后续环节或 DAC 发生硬削波失真。支持“共享单增益”和“每通道独立”两种模式。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `mode` | string | shared | `shared` 全通道共用一组 attack/release；`per_channel` 每通道独立系数。 |
| `threshold_db` | float | -6.0 dB | 限幅阈值。超过此电平开始压限。 |
| `attack_ms` | float | 5.0 ms | shared 模式下的启动时间。 |
| `release_ms` | float | 100.0 ms | shared 模式下的释放时间。 |
| `attack_coeffs` | string | "0.0237" | per_channel 模式下每通道的 attack 系数。 |
| `release_coeffs` | string | "1.00024" | per_channel 模式下每通道的 release 系数。 |
| `k1` | string | "0.01185" | per_channel 模式下的额外比例系数。 |
| `max_attack` | string | "0.31623" | per_channel 模式下的最小增益下限。 |
| `channels` | int | 2 | 通道数，改变后需重新编译。 |

### `mode = shared`

所有通道共用同一个包络， gain 同时作用于所有通道。优点是不会因为某一通道突发峰值导致立体声像漂移；缺点是通道间会互相牵制。

### `mode = per_channel`

每个通道有独立的包络和增益。公式更复杂：

```
gain = k1[c] * threshold / env[c]
gain = max(gain, max_attack[c])
```

适合多通道独立保护，例如环绕声各声道单独压限。

### 系数 vs 时间的换算

`attack_coeffs` / `release_coeffs` 不是毫秒，而是一阶平滑系数。系数越大响应越快。如果要从时间常数换算，可近似：

```
coeff ≈ 1 - exp(-1 / (tau * sample_rate))
tau = time_ms / 1000
```

### `k1` 和 `max_attack` 的作用

- `k1` 会把阈值再按比例缩放，相当于让限幅更“软”或更“狠”。
- `max_attack` 限制最大衰减量，避免增益降得过低导致声音突然“抽空”。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，通道数与输入相同。

## 注意事项

- shared 模式下 `attack_ms` / `release_ms` 是 smoothed 的，可以实时调；per_channel 系数是字符串列表，改后需重新编译。
- `set_parameter` 中重新计算系数时固定按 48000 Hz 计算，因此运行时改动 attack/release 在 non-48k 工程中会有轻微偏差。
- 限幅器只限峰值，不限制长期能量；如需控制感知响度，请配合 `level_detect` + `gain` 使用。

## 典型用法

```
source ──► limiter(threshold_db=-1.0, mode=shared,
                   attack_ms=0.1, release_ms=50)
       ──► soft_clipper ──► wav_out
```

这是保护最终输出的常见链路：限幅器先把大峰值压住，软削波再进一步柔化顶部，避免 WAV 输出出现硬削波。
