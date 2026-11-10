# orpheus.builtin.midrange — 中频音调控制

## 功能

`component.yaml.interfaces` 是 ABI7 端口、参数与资源槽的唯一事实源。
用 `scripts\generate_component.py components\orpheus\builtin\midrange\component.yaml`
生成 `generated/`；算法与私有历史在 `user/algorithm.c/h`，仅通过 `emit_out` 交付。
create 只初始化宿主提供的 State，宿主明确 prepare 一次。
壳严格拒绝声明外类型（含 int 隐式转 float）、越界与非有限数值。
运行时仅 `gain_db` 可更新，读回当前平滑增益，注册槽保存目标。
reset 仅清两个滤波历史，不跳变当前增益或目标；prepare 重算系数并加载初始增益。
无新增探针或动态资源，不保留旧 DLL/descriptor 适配。

二阶带通搁架音调控制：只提升或衰减弱某一频段（中心频率 `fc`、品质因数 `q`）的声音，中高频和低频保持原样。与 `bass`（低频搁架）和 `treble`（高频搁架）组合，构成经典三段音调控制。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `gain_db` | float | 0.0 dB | 中心频段增益。`-12` ~ `+12` dB。 |
| `fc` | float | 1000 Hz | 中心频率。 |
| `q` | float | 1.0 | 品质因数，控制带宽窄。 |
| `channels` | int | 2 | 通道数，改变后需重新编译。 |
| `ramp_ms` | float | 50.0 ms | `gain_db` 变化的斜坡时间。 |

### `gain_db` 的真实含义

内部使用二阶带通滤波器提取中心频段分量，然后按 `boost = 10^(gain_db/20) - 1` 进行提升或衰减。输出 = 输入 + `boost` × 带通分量。

- `gain_db = 0`：完全直通。
- `gain_db = +6`：中心频段提升约 6 dB。
- `gain_db = -6`：中心频段衰减约 6 dB。

### `q` 的选择

- `q = 0.5`：很宽的钟形曲线，影响范围可达数倍频程。
- `q = 1.0`：中等宽度，适合一般中音调节。
- `q = 5.0`：很窄，接近 notch/peaking EQ，可用于修正特定啸叫频率。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，通道数与输入相同。

## 注意事项

- 使用 Direct Form II Transposed 结构实现二阶 IIR，每个通道有 `z1`、`z2` 两个状态变量。
- `fc` 和 `q` 是 `restart_required`，改后需重新编译；只有 `gain_db` 可实时平滑调节。
- 滤波器系数按工程采样率计算，所以同一个参数在不同采样率下听感一致（以频率为基准）。

## 典型用法

```
source ──► bass(fc=200) ──► midrange(fc=1000, q=1.0)
       ──► treble(fc=4000) ──► out
```

标准三段音调链路：先调低音乐器体重，再调人声清晰度（1 kHz 附近），最后调镲片/空气感。
