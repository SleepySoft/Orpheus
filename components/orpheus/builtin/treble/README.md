# orpheus.builtin.treble — 高频音调控制

## 功能

`component.yaml.interfaces` 是 ABI7 端口、参数与资源槽的唯一事实源。
用 `scripts\generate_component.py components\orpheus\builtin\treble\component.yaml`
生成 `generated/`；算法与私有历史在 `user/algorithm.c/h`，仅通过 `emit_out` 交付。
create 只初始化宿主提供的 State，宿主明确 prepare 一次。
壳严格拒绝声明外类型（含 int 隐式转 float）、越界与非有限数值。
运行时仅 `gain_db` 可更新，读回当前平滑增益，注册槽保存目标。
reset 仅清滤波历史，不跳变当前增益或目标；prepare 重新计算系数并加载初始增益。
无新增探针或动态资源，不保留旧 DLL/descriptor 适配。

一阶高频搁架（high-shelf）音调控制：只提升或衰减高频部分，低频保持原样。与 `bass`（低频搁架）、`midrange`（中频带通）组成经典三段音调控制。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `gain_db` | float | 0.0 dB | 高频增益。`-12` ~ `+12` dB，可实时平滑变化。 |
| `fc` | float | 4000 Hz | 搁架转折频率。 |
| `channels` | int | 2 | 通道数，改变后需重新编译。 |
| `ramp_ms` | float | 50.0 ms | `gain_db` 变化的斜坡时间。 |

### `gain_db` 的真实含义

内部用一阶 IIR 低通提取低频分量，再用“输入 - 低频”得到高频分量，然后按 `boost = 10^(gain_db/20) - 1` 提升或衰减高频：

```
output = input + boost * (input - lowpass)
```

- `gain_db = 0`：完全直通。
- `gain_db = +6`：高频约提升 6 dB。
- `gain_db = -6`：高频约衰减 6 dB。

### `fc` 的选择

- 2~4 kHz：影响人声清晰度和镲片亮度。
- 8~12 kHz：影响“空气感”和极高频细节。
- `fc` 越高，受影响的频段越靠上。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，通道数与输入相同。

## 注意事项

- 与 `bass` 对称实现，只是一个处理低频、一个处理高频。
- `fc` 和 `ramp_ms` 是 `restart_required`；只有 `gain_db` 可实时调。
- 一阶搁架的过渡带较宽，如果需要更精确的高频 EQ 曲线，请使用 `iir_bank`。

## 典型用法

```
source ──► bass(fc=200) ──► midrange(fc=1000) ──► treble(fc=4000) ──► out
```

经典三段音调链路，分别控制低、中、高频音色。
