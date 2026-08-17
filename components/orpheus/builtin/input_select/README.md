# orpheus.builtin.input_select — 输入选择

## 功能

通道选择/解复用器：从输入的 `channels_in` 个通道中，按用户指定的映射挑选出 `channels_out` 个通道。常用于“30 进 12 出”的输入路由、抽取特定通道、或把多通道总线中的某几路转发到子系统。

与 `output_router` 的区别：`input_select` 是“每路输出选一路输入”；`output_router` 是“每路输出混多路输入”。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels_in` | int | 2 | 输入通道数，改变后需重新编译。 |
| `channels_out` | int | 2 | 输出通道数，改变后需重新编译。 |
| `select` | string | "1,2" | 输出到输入的映射表，1 起始索引，0 表示静音。 |

### `select` 的真实含义

`select` 是一个逗号分隔的整数列表，长度通常为 `channels_out`。第 `o` 个值表示输出通道 `o` 取自输入通道 `select[o] - 1`。

示例：`channels_in=6, channels_out=4, select="1,2,3,5"`

- 输出 0 ← 输入 0
- 输出 1 ← 输入 1
- 输出 2 ← 输入 2
- 输出 3 ← 输入 4

如果某个值为 `0` 或超出输入范围，该输出通道为静音（0）。

如果 `select` 列表比 `channels_out` 短，未指定的输出通道保持默认身份映射（输出 `o` ← 输入 `o`）。

## 端口

- `in`: 输入音频，`channels_in` 通道。
- `out`: 输出音频，`channels_out` 通道。

## 注意事项

- `select` 是 1 起始索引，与 C 数组的 0 起始不同，写映射时容易踩坑。
- 所有路由参数都属于 `restart_required`，运行时不能热切换映射。需要切换时请用多个 `input_select` 加 `switch` 组合，或重新编译工程。
- 输出通道数可以与输入不同，支持升维（重复输入）或降维（丢弃输入）。

## 典型用法

```
music_bus (30ch) ──► input_select(channels_in=30, channels_out=12,
                                 select="1,2,3,4,5,6,7,8,9,10,11,12")
                 ──► pre_amp
```

从 30 通道音乐总线中抽取前 12 路交给预放处理，是 Symphony 工程中 `input_select` 子组件的典型用法。
