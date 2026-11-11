# orpheus.builtin.output_router — 静态矩阵路由

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

空文本也保留单位阵。非空短数值列表从首项覆盖单位阵，
**未提供的系数保留原单位阵值**，并非全部补零。
数值按 `strtof` 解析，允许逗号或空白分隔；非法数值、非有限值、
空项、尾逗号、超过 `channels_in*channels_out` 项及超长文本明确拒绝。
文本容量为 32768 字节，不再按旧 2048 字节临时缓存静默截断，
可完整配置 32×32 非平凡系数。非法候选保留原矩阵与配置回读。

## 端口

- `in`: 输入音频及驱动入口，`channels_in` 通道。
- `out`: 输出音频及 1 号驱动出口，`channels_out` 通道。

每次有效输入到达均同步交付，同一 Task 的重复到达分别处理。
零帧不交付；输入不足、输入输出重叠或输出覆盖矩阵存储明确拒绝。
接口配置经独立脚本生成壳，手写算法保存在 `user/algorithm.c`。

## 注意事项

- 所有路由参数都属于 `restart_required`，运行时不能热更新。
- reset 保留当前矩阵；物理矩阵按 32 项固定行步长保存。
- 与 `matrix_mul` 不同：短列表保留单位阵尾部、不开放运行期 BULK 更新，
  且矩阵中正负零增益会跳过乘法，因此未路由输入的 NaN/Inf 不会污染输出。

## 典型用法

```
feedback (22ch) ──► output_router(channels_in=22, channels_out=30, matrix=...)
                ──► music_mixer
```

外部参考工程中用 `output_router` 把 22 路反馈信号扩展成 30 路，以便与原始音乐信号对齐后混合。
