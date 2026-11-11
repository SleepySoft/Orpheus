# orpheus.builtin.input_select — 固定索引通道选择

## 功能

通道选择/解复用器：从输入的 `channels_in` 个通道中，按用户指定的映射挑选出 `channels_out` 个通道。常用于“30 进 12 出”的输入路由、抽取特定通道、或把多通道总线中的某几路转发到子系统。

与 `output_router` 的区别：`input_select` 是“每路输出选一路输入”；`output_router` 是“每路输出混多路输入”。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels_in` | int | 2 | 输入通道数，改变后需重新编译。 |
| `channels_out` | int | 2 | 输出通道数，改变后需重新编译。 |
| `select` | string | "" | 空值为顺序映射；非空列表使用 1 起始索引，非正值和越界表示静音。 |
| `override_select` | string | "" | 运行期覆盖表：0=沿用 `select`，有效 1-based 值覆盖，越界/负数静音。 |

### `select` 的真实含义

`select` 是一个逗号分隔的整数列表，长度通常为 `channels_out`。第 `o` 个值表示输出通道 `o` 取自输入通道 `select[o] - 1`。

示例：`channels_in=6, channels_out=4, select="1,2,3,5"`

- 输出 0 ← 输入 0
- 输出 1 ← 输入 1
- 输出 2 ← 输入 2
- 输出 3 ← 输入 4

基础 `select` 中某个值为 `0` 或超出输入范围时，该输出通道为静音（0）。

如果 `select` 列表比 `channels_out` 短，未指定的输出通道保持默认身份映射（输出 `o` ← 输入 `o`）。

`override_select` 在块边界更新 effective map。第 `o` 项为 0 时保留基础 `select[o]`；正数使用新的 1-based 输入索引；负数或越界值静音。列表未覆盖的位置同样保留基础映射。

两张表均为最多 255 UTF-8 字节的整数 CSV，允许数值两侧空白和短列表；
超过输出通道数、空项、尾随逗号、非整数或超出 int32 范围明确失败，
不跳过垃圾字符或截断字符串。非法 prepare 候选或运行期覆盖保留旧映射。
空覆盖恢复基础映射；reset 保留当前覆盖与基础映射。

## 端口

- `in`: 输入音频及驱动入口，`channels_in` 通道。
- `out`: 输出音频及 1 号驱动出口，`channels_out` 通道。

每次有效输入到达后同步交付输出，同一 Task 的重复到达分别处理，
不等待其他入口或去重。零帧不交付；输入不足或输入输出重叠明确拒绝。
有效通道逐位复制（包括 NaN、Inf 和负零），静音为正零。
接口以 `component.yaml` 为事实源，生成壳与手写 `user/algorithm.c` 分开维护。

## 注意事项

- `select` 是 1 起始索引，与 C 数组的 0 起始不同，写映射时容易踩坑。
- `select` 和签名参数属于 `restart_required`；`override_select` 可在运行时块边界更新，不改变端口签名。
- 输出通道数可以与输入不同，支持升维（重复输入）或降维（丢弃输入）。

## 典型用法

```
music_bus (30ch) ──► input_select(channels_in=30, channels_out=12,
                                 select="1,2,3,4,5,6,7,8,9,10,11,12")
                 ──► pre_amp
```

从 30 通道音乐总线中抽取前 12 路交给预放处理，是外部参考工程中 `input_select` 子组件的典型用法。
