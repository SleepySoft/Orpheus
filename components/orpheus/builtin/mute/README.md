# orpheus.builtin.mute — 静音渐变

## 功能

通过平滑增益 ramps 实现静音/恢复，避免开关瞬间产生咔哒声。`mute <= 0.5` 时恢复正常，`mute > 0.5` 时静音；中间值不是线性音量。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `mute` | float | 0.0 | `0` = 正常，`1` = 静音，可平滑过渡。 |
| `channels` | int | 2 | 通道数，改变后需重新编译。 |
| `ramp_ms` | float | 20.0 ms | 静音切换的斜坡时间。 |

### `mute` 的真实含义

内部逻辑：

```
target = (mute > 0.5) ? 0.0 : 1.0
gain += coeff * (target - gain)
output = input * gain
```

因为阈值是 0.5，所以：

- `mute = 0.0`：完全正常。
- `mute = 0.4`：仍然视为正常（target = 1.0）。
- `mute = 0.6`：视为静音（target = 0.0）。
- `mute = 1.0`：完全静音。

它本质上是一个“硬判断 + 软过渡”的开关。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，通道数与输入相同。

## 注意事项

- `mute` 是 smoothed 参数，可以在运行时实时切换。
- `ramp_ms` 只在 prepare 时读取，运行时修改无效。
- 与 `switch` 组件非常相似，但 `mute` 的语义更偏向“静音/恢复”；`switch` 更偏向“使能/关闭”。两者实现几乎一致。

## 典型用法

```
main_chain ──► mute ──► output
```

把 `mute` 放在主输出前，用户点击静音按钮时不会产生咔哒声。也常用在测试场景，快速关闭某一路子系统。

## 配置驱动接口（ABI v7）

`component.yaml` 的 `interfaces` 是唯一接口事实源；修改后运行
`python scripts\generate_component.py components\orpheus\builtin\mute\component.yaml`。
`generated/` 是脚本生成的元数据和 ABI 壳，`user/algorithm.c/.h` 是手写算法与私有包络状态。
`in` 为唯一驱动输入；保留原核按**交错样本**推进包络的顺序（不是每帧一次），
整块计算后显式 `emit_out`，稳定出口编号为 `1`，通过 delivery 交付，不会合、不去重。
prepare/reset 将当前增益直接对齐静音目标，运行期更新只改变目标。

参数按声明严格校验；越界、非有限值或错误类型返回 `ORPHEUS_ERR_INVALID_ARG`，
不再隐式 clamp/类型转换；`mute` 必须传 float，不接受 bool/int。
