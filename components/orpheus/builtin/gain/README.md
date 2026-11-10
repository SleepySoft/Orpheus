# orpheus.builtin.gain — 增益

## 功能

最基础的音量/增益控制：把输入音频乘以一个 dB 标定的线性增益。支持实时平滑过渡，避免参数突变带来的咔哒声。

几乎所有音频链路都需要它：输入补偿、输出补偿、子系统级 headroom 调整、自动化音量包络等。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `gain_db` | float | 0.0 dB | 目标增益。`-96` ~ `+24` dB。 |
| `channels` | int | 2 | 通道数，改变后需重新编译。 |
| `smoothing_ms` | float | 5.0 ms | `gain_db` 变化时的平滑过渡时间。 |

### `gain_db` 的真实含义

内部公式：

```
linear = 10^(gain_db / 20)
output = input * linear
```

- `0 dB`：直通，无变化。
- `+6 dB`：线性放大到约 2 倍。
- `-6 dB`：衰减到约 0.5 倍。
- `-96 dB`：接近静音（线性约 0.000016）。

### `smoothing_ms` 的真实含义

每次 process 都会把当前线性增益向目标值移动一阶指数步进：

```
gain += coeff * (target - gain)
coeff = 1 - exp(-1 / (tau * sample_rate))
tau = smoothing_ms / 1000
```

- `0`：立即跳变，可能产生咔哒声。
- `5 ms`：快速但基本无咔哒，适合实时旋钮。
- `100 ms`：很慢的淡入淡出。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，通道数与输入相同。

## 注意事项

- 该组件支持**原地处理**（supports_inplace），意味着 Runtime 可以把输入输出指向同一块内存，节省 buffer。
- 参数变化是 smoothed 的，但 `smoothing_ms` 本身改后需要重新编译；运行时只能改 `gain_db`。
- reset 会把增益重置为 0 dB，而不是当前参数值。

## 典型用法

```
signal_gen ──► gain(gain_db=-12) ──► soft_clipper ──► wav_out
```

先生成信号，再衰减 12 dB 给后级留出 headroom，最后软削波输出。这是防止数字削波的经典链路。

## 接口与算法边界

`component.yaml` 的 `interfaces` 是唯一外部声明；独立脚本生成
`generated/`，算法及平滑状态位于 `user/`。输入 `in` 是处理入口，
算法完成本次有效帧后交付编号 1 的 `out`，不再调用节点级续调。

模式 0 保持 dB 平滑和 reset 回到 0 dB；模式 1 保持逐帧共享线性递推，
reset 清递推状态但保留线性目标；模式 2 保持逐通道直接相乘和系数 reset 保留。
`gains` 的初始化 CSV 与运行期浮点 BULK 在同一接口内声明，
仅模式 2 通过生成的类型化 helper 注册资源，有效数量为 `channels`，容量上限 32。
字符串由实例持有，最多 4095 个 UTF-8 字节；运行期系数写入走双 Bank。

ABI v7 严格拒绝声明外类型、非有限值与越界设置，不再依赖旧隐式转换。
