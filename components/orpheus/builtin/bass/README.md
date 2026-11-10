# orpheus.builtin.bass — 低频音调控制

## ABI7 接口交付

`component.yaml.interfaces` 是端口、参数与资源槽的唯一事实源；运行
`.\.venv\Scripts\python.exe scripts\generate_component.py components\orpheus\builtin\bass\component.yaml`
生成 `generated/`，手写算法与私有历史位于 `user/algorithm.c/h`。宿主提供 State 内存，
create 只初始化字段，宿主随后明确 prepare 一次；算法通过 `emit_out` 实际交付输出。
壳严格拒绝声明外类型（包括 int 隐式转 float）、越界与非有限数值。
运行时仅 `gain_db` 可更新；读回为当前平滑增益，注册槽保存目标值。
reset 仅清滤波历史，不跳变当前增益或目标；prepare 重新计算系数并加载初始增益。
无新增探针或动态资源，不保留旧 DLL/descriptor 适配。

## 功能

一阶低频搁架（low-shelf）音调控制：只提升或衰减低频部分，中高频保持原样。核心公式是“直通 + 增益 × 低通分量”，因此 `gain_db = 0` 时输出与输入完全一致。

适用于车载音响低音补偿、家庭影院 LFE 微调，或任何只需要整体“加厚/削薄低频”的场景。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `gain_db` | float | 0.0 dB | 低频增益。`-12` ~ `+12` dB，可实时平滑变化。 |
| `fc` | float | 200 Hz | 搁架转折频率。需重新编译（restart_required）。 |
| `channels` | int | 2 | 通道数，改变后需重新编译。 |
| `ramp_ms` | float | 50.0 ms | `gain_db` 变化的斜坡时间。 |

### `gain_db` 的真实含义

内部先把 dB 转成线性增益 `G`，再算出“提升量” `boost = G - 1`。输出 = 输入 + `boost` × 低通后的输入。

- `gain_db = 0` → `boost = 0` → 完全直通。
- `gain_db = +6` → 低频大约提升 6 dB。
- `gain_db = -6` → 低频大约衰减 6 dB。

注意这里提升/衰减的量是对低频区域而言，高频几乎不受影响。

### `fc` 的选择

- 100 Hz 以下：主要影响“隆隆声”和鼓的体重感。
- 200 Hz 左右：影响低音力度和人声温暖度。
- 500 Hz 以上：开始进入中低频，容易让声音变浑浊。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，通道数与输入相同。

## 注意事项

- 该组件基于一阶 IIR 低通实现，计算量小，但搁架曲线不如二阶搁架陡峭。如果需要更精确控制低频形状，请使用 `iir_bank`。
- `fc` 与采样率有关， prepare 时会根据工程采样率重新计算滤波器系数。
- 每个通道有独立的一阶状态变量，多通道互不干扰。

## 典型用法

```
source ──► bass(fc=150Hz, gain_db=+3) ──► midrange ──► treble ──► out
```

这是一条经典的三段音调控制链路：先调低音“厚度”，再调中音“存在感”，最后调高音“明亮度”。
