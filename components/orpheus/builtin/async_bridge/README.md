# orpheus.builtin.async_bridge - 异步任务桥

## 功能

连接不同 Task 的音频流。生产 Task 把完整音频块写入固定容量 SPSC Ring Buffer，消费 Task 按自己的块长读取；队列为空时补零，队列满时丢弃当前写入块并记录探针。

## 参数

| 参数 | 说明 |
|---|---|
| `channels` | 音频通道数；改变后需重新编译。 |
| `capacity_frames` | Ring Buffer 容量。`0` 表示由编译器根据上下游块长自动计算。 |
| `level_frames` | 当前队列水位，只读探针。 |
| `underruns` | 消费时数据不足的累计次数，只读探针。 |
| `overruns` | 生产时空间不足的累计次数，只读探针。 |

## 端口

- `in`：生产 Task 写入的音频块。
- `out`：消费 Task 读取的音频块，块长采用桥节点所属 Task 的 `block_size`。

## 注意事项

- 节点必须归属于消费 Task，上游节点必须属于另一个 Task。
- 普通音频线不允许直接跨 Task；必须通过本组件形成明确同步边界。
- Runtime 的 Task 入口由调用方串行调度；并发宿主接入时 Ring Buffer 是唯一共享数据面。
