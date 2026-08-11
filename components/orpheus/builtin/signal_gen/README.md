# orpheus.builtin.signal_gen — 信号发生器

## 功能

生成合成正弦波信号，作为测试音或算法的激励源。它同时是工程的“时钟源”（clock_source: synthetic），意味着没有外部音频输入时，它驱动整个图按 `sample_rate` 和 `block_size` 运行。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `sample_rate` | int | 48000 Hz | 生成信号的采样率，改变后需重新编译。 |
| `frequency` | float | 440.0 Hz | 正弦波频率。 |
| `amplitude` | float | 0.5 | 输出幅度，范围 0~1。 |
| `channels` | int | 2 | 输出通道数，改变后需重新编译。 |

### `sample_rate` 与工程采样率的关系

该组件声明自己是合成时钟源，因此 `out` 端口的 `sample_rate` 由 `sample_rate` 参数决定。如果它与工程 `sample_rate` 不一致，Runtime 会按端口采样率处理，通常应该保持两者一致。

### `amplitude` 的真实含义

输出正弦波的峰值幅度。`amplitude = 0.5` 表示输出在 `-0.5` 到 `+0.5` 之间。如果要产生满幅测试音，可设为 `1.0`。

### 频率范围

支持 1 Hz ~ 20 kHz。超过奈奎斯特频率（`sample_rate / 2`）时会产生混叠，因此高频测试音需要配合高采样率使用。

## 端口

- `out`: 输出音频，`channels` 通道。该组件没有输入端口。

## 注意事项

- 只支持正弦波；如果需要噪声、方波等波形，需要组合其他组件或自定义组件。
- `frequency` 是 `restart_required`，运行时不能改。如需扫频，请在工程外生成 WAV 再用 `wav_in`（如果存在）或构建自定义组件。
- `amplitude` 是 smoothed，可以实时拖动，但内部实现是立即赋值，没有指数斜坡。

## 典型用法

```
signal_gen(frequency=1000, amplitude=0.2, channels=2) ──► gain ──► wav_out
```

生成 1 kHz 测试音，衰减后写入 WAV，用于验证链路基线失真和电平。
