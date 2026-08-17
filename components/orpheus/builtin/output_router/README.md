# orpheus.builtin.output_router — 输出路由

## 功能

矩阵混音路由：每一路输出可以是多路输入按不同增益加权求和的结果。相比 `input_select`（每路输出只能选一路输入），`output_router` 实现真正的矩阵混音，是构建复杂通道映射、上混、下混、反馈路径的核心组件。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels_in` | int | 2 | 输入通道数，改变后需重新编译。 |
| `channels_out` | int | 2 | 输出通道数，改变后需重新编译。 |
| `matrix` | string | "identity" | 路由矩阵，行优先逗号分隔。 |

### `matrix` 的排列方式

矩阵大小为 `channels_out × channels_in`，按行优先填充：

```
out[o] = Σ matrix[o * cols + i] * in[i]
```

示例：`channels_in=2, channels_out=4, matrix="1,0,0,1,0.5,0.5,0.7,0.3"`

- 输出 0 = 1×in0 + 0×in1 = in0
- 输出 1 = 0×in0 + 1×in1 = in1
- 输出 2 = 0.5×in0 + 0.5×in1
- 输出 3 = 0.7×in0 + 0.3×in1

### "identity" 特殊值

当 `matrix = "identity"` 时，自动构建最小维度单位矩阵：输出 `i` = 输入 `i`，超出输入范围的输出为 0。

## 端口

- `in`: 输入音频，`channels_in` 通道。
- `out`: 输出音频，`channels_out` 通道。

## 注意事项

- 矩阵元素数量超过 `channels_in * channels_out` 时，多余部分被忽略；不足时剩余元素保持 0。
- 所有路由参数都属于 `restart_required`，运行时不能热更新。
- 与 `matrix_mul` 不同：`output_router` 的矩阵元素是字符串形式的线性增益，且输入输出通道数独立；`matrix_mul` 的参数 `rows`/`cols` 本身就是输入输出维度。
- 矩阵中为 0 的元素会跳过乘法，稍微节省计算量。

## 典型用法

```
feedback (22ch) ──► output_router(channels_in=22, channels_out=30, matrix=...)
                ──► music_mixer
```

Symphony 工程中用 `output_router` 把 22 路反馈信号扩展成 30 路，以便与原始音乐信号对齐后混合。
