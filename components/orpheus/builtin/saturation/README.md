# orpheus.builtin.saturation — 饱和限幅

## 功能

逐样本饱和限幅：把信号限制在 `[-limit, +limit]` 范围内，并可通过 `soft` 参数在“硬削波”和“tanh 软饱和”之间连续过渡。常用于给数字信号添加模拟感的饱和色彩，或作为简易限幅器使用。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `limit` | float | 0.5 | 限幅上限。信号会被限制在 `[-limit, +limit]`。 |
| `soft` | float | 0.0 | 软饱和比例。`0` = 硬削波，`1` = 纯 tanh 软饱和。 |
| `channels` | int | 2 | 通道数，改变后需重新编译。 |

### `soft` 的真实含义

内部把“硬限幅结果”和“tanh 饱和结果”按 `soft` 混合：

```
hard = clamp(x, -limit, +limit)
soft_y = limit * tanh(x / limit)
output = (1 - soft) * hard + soft * soft_y
```

- `soft = 0`：硬削波，超过 limit 的部分直接切掉，会产生丰富的高次谐波。
- `soft = 1`：tanh 软饱和，信号越接近 limit 越被压缩，音色更圆润。
- `0 < soft < 1`：混合两种效果。

### `limit` 不是阈值而是上限

无论输入多大，输出绝对值都不会超过 `limit`。如果把它接在 `gain` 后面作为效果器，可以先把信号放大再轻微饱和，得到更明显的染色。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，通道数与输入相同。

## 注意事项

- `limit` 和 `soft` 都是 smoothed 参数，可以实时拖动。
- 硬削波会引入高频谐波，若后续有低采样率路径，请注意混叠。
- 与 `soft_clipper` 不同：`saturation` 直接把信号限制在固定阈值内；`soft_clipper` 通过 `drive` 先放大再 tanh，并做归一化，更像“失真效果器”。

## 典型用法

```
source ──► gain(gain_db=+6) ──► saturation(limit=0.5, soft=0.3) ──► out
```

先把信号提升获得 headroom，再用轻度软饱和增加厚度，最后输出。
