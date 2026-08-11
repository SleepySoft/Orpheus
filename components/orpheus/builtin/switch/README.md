# orpheus.builtin.switch — 开关

## 功能

带平滑过渡的音频开关：`enable = 1` 时直通，`enable = 0` 时静音。与 `mute` 组件实现几乎一致，但语义更偏向“功能使能/旁路”。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `enable` | float | 1.0 | `0` = 关闭（静音），`1` = 开启（直通）。 |
| `channels` | int | 2 | 通道数，改变后需重新编译。 |
| `ramp_ms` | float | 20.0 ms | 开关切换的斜坡时间。 |

### `enable` 的真实含义

阈值判断：

```
target = (enable >= 0.5) ? 1.0 : 0.0
```

因此只有跨越 0.5 时才会真正切换。这种设计配合 checkbox 控件很直观。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，通道数与输入相同。

## 注意事项

- `enable` 可实时变化，切换时有斜坡保护。
- `ramp_ms` 在 prepare 时读取，运行时改无效。
- reset 会把 `enable` 重置为 1.0，即默认开启。

## 典型用法

```
source ──► switch(enable=1) ──► effect_chain ──► out
```

用 `switch` 控制整条效果链的开启/关闭，比直接断开连线更安全，不会产生咔哒声。
