# orpheus.builtin.soft_clipper — 软削波

## 功能

基于 `tanh` 的软削波效果器：先把输入按 `drive_db` 放大，再通过 `tanh` 饱和，最后做归一化。小信号近似线性，大信号被平滑压缩。常用于给数字音频添加模拟饱和色彩，或作为输出级最后的温和保护。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `drive_db` | float | 0.0 dB | 输入增益/驱动量。 |
| `channels` | int | 2 | 通道数，改变后需重新编译。 |

### `drive_db` 的真实含义

内部流程：

```
drive_linear = 10^(drive_db / 20)
x = input * drive_linear
norm = 1 / tanh(drive_linear)
output = tanh(x) * norm
```

- `drive_db = 0`：drive_linear = 1，norm = 1/tanh(1) ≈ 1.31，输出 ≈ tanh(input) × 1.31。小信号略有增益，大信号被压缩。
- `drive_db = +12`：输入先放大到约 4 倍，再 tanh，饱和更明显。
- `drive_db = -12`：输入衰减，接近线性区。

归一化保证当输入为满幅 1.0 时，输出也接近 1.0，不会出现整体音量骤降。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，通道数与输入相同。

## 注意事项

- `drive_db` 是 smoothed，可以在运行时实时拖动；但 process 内部每次都会重新计算 `drive_linear` 和 `norm`，没有增益斜坡，快速拖动可能产生轻微噪声。
- 与 `saturation` 的区别：`soft_clipper` 通过 drive 控制“推入 tanh 的深度”并做归一化；`saturation` 直接限制在固定 `limit` 内，并混合硬/软两种模式。
- tanh 软削波会引入少量偶次/奇次谐波，适合作为音色工具而非精确保护。

## 典型用法

```
source ──► gain(gain_db=-3) ──► soft_clipper(drive_db=6) ──► wav_out
```

给音乐信号轻微过载，增加中频密度，同时保持输出峰值可控。
