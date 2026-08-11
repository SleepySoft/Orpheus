# orpheus.builtin.mixer — 混音器

## 功能

两路同通道数音频按各自 dB 增益混合成一路输出：`out = in0 * gain0 + in1 * gain1`。最简单的用途是“把两条信号流加在一起”，例如把音乐与反馈信号合并、把干声与湿声混合、或把多支麦克风合并。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `gain0` | float | 0.0 dB | 第一路输入增益。 |
| `gain1` | float | 0.0 dB | 第二路输入增益。 |
| `channels` | int | 2 | 三路端口的通道数，改变后需重新编译。 |

### `gain0` / `gain1` 的真实含义

内部先把 dB 转成线性增益：

```
linear = 10^(gain_db / 20)
```

然后每样本做：`out = in0 * linear0 + in1 * linear1`。

- `0 dB` = 原音量加入。
- `-96 dB` ≈ 静音。
- `+6 dB` = 2 倍加入。

### 两路都打开时要注意

如果两路信号内容相同且都设为 0 dB，输出幅度会变成原来的 2 倍（+6 dB），很容易削波。混合前通常需要把每路都衰减 3~6 dB，或在 mixer 后接限幅器。

## 端口

- `in0`: 第一路输入音频，`channels` 通道。
- `in1`: 第二路输入音频，`channels` 通道。
- `out`: 混合后的输出音频，`channels` 通道。

## 注意事项

- 两个输入必须都连接；如果任一输入未连接，process 会返回错误。
- 参数变化是 smoothed 的，但 `smoothing_coeff` 在 prepare 中固定为 1.0，**实际上增益是立即跳变到目标值的**（没有内部斜坡）。所以快速拖动 `gain0` 可能产生轻微噪声。如果需要平滑过渡，请在前面加 `gain_ramper`。
- 不支持原地处理。

## 典型用法

```
music_src ──► mixer(gain0=0, gain1=0) ◄── feedback_delay
          ──► wav_out
```

把原始音乐信号与反馈延迟后的信号合并，是 BAF SAS 工程中 `music_mixer` 节点的经典用法。
