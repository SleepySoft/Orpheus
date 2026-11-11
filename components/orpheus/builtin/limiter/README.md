# orpheus.builtin.limiter — 峰值增益限幅

## 功能

峰值限幅器：当信号幅度超过设定阈值时，按包络动态降低增益。支持共享包络、逐通道包络和双参数组反馈三种模式；`in` 是驱动入口，每次真实到达后自主交付1号 `out`。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `mode` | string | shared | `shared` 共享包络；`per_channel` 逐通道包络；`feedback` 双参数组反馈增益。 |
| `threshold_db` | float | -6.0 dB | 限幅阈值。超过此电平开始压限。 |
| `attack_ms` | float | 5.0 ms | shared 模式下的启动时间。 |
| `release_ms` | float | 100.0 ms | shared 模式下的释放时间。 |
| `attack_coeffs` | string | "0.0237" | per_channel 模式下每通道的 attack 系数。 |
| `release_coeffs` | string | "1.00024" | per_channel 模式下每通道的 release 系数。 |
| `k1` | string | "0.01185" | per_channel 模式下的额外比例系数。 |
| `max_attack` | string | "0.31623" | per_channel 模式下的最小增益下限。 |
| `channels` | int | 2 | 通道数，改变后需重新编译。 |

### `mode = shared`

所有通道按交错样本顺序推进同一个包络，每个样本应用其当时的增益；不是先扫描整帧，再对所有通道施加同一帧增益。

### `mode = per_channel`

每个通道有独立的包络和增益。公式更复杂：

```
gain = k1[c] * threshold / env[c]
gain = max(gain, max_attack[c])
```

适合多通道独立保护，例如环绕声各声道单独压限。
系数列表支持逗号/空白分隔，缺项重复最后一个值；空字符串使用默认系数。
完整解析并拒绝垃圾、非有限值与超过32项的文本，不再将无效词重复解析为零。

### `mode = feedback`

`feedback_attack`、`feedback_release`、`feedback_k1`、`feedback_min_factor`
分别声明 `2×channels` 的f32双缓冲资源，物理容量64项。
初始化字符串为空时使用默认值，非空时必须提供完整两组CSV。
`parameter_set` 选择1/2组，`disabled` 逐位直通并冻结历史增益、清本块位图。
`active_mask` 是只读32位通道位图，`gain` 是只读的最后通道/样本增益。
零帧不推进历史、也不清原位图，但仍交付零帧事件；reset恢复单位反馈增益，
保留参数组与旁路配置。其它两种模式reset恢复零包络。

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
- 精确原地合法；部分重叠、与组件状态重叠及不足载荷拒绝。下游失败不回滚已处理历史。
- 接口声明是事实源；独立运行 `scripts/generate_component.py` 生成壳，算法只填音频并显式交付，不保留 `next` 入口。

## 典型用法

```
source ──► limiter(threshold_db=-1.0, mode=shared,
                   attack_ms=0.1, release_ms=50)
       ──► soft_clipper ──► wav_out
```

这是保护最终输出的常见链路：限幅器先把大峰值压住，软削波再进一步柔化顶部，避免 WAV 输出出现硬削波。
