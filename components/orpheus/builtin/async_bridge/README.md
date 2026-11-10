# orpheus.builtin.async_bridge - 异步任务桥

## 功能

连接不同 Task 的音频流。生产 Task 将每次 Delivery 的有效片段同步复制到固定容量
SPSC Ring Buffer，消费 Task 按自己的块长读取；数据不足时补零。
空间不足时整体丢弃本次片段并记录溢出，不丢弃同一 Task 中此前已成功入队的交付。

## 参数

| 参数 | 说明 |
|---|---|
| `channels` | 音频通道数；改变后需重新编译。 |
| `capacity_frames` | Ring Buffer 容量。`0` 表示由编译器根据上下游块长自动计算。 |
| `level_frames` | 当前队列水位，只读探针。 |
| `underruns` | 消费时数据不足的累计次数，只读探针。 |
| `overruns` | 生产时空间不足的累计次数，只读探针。 |

## 端口

- `in`：生产 Task 写入的音频片段，`process: false`，只供数、不触发消费。
- `tick`：纯触发消费入口，`process: true`，不分配 PCM Buffer。
- `out`：出口编号 1，消费 Task 读取的音频块，块长采用所属 Task 的 `block_size`。

## 驱动边界

上游交付仅入队，不因写满而触发读取。消费驱动连接到 `tick`，
组件消费后在算法调用点显式交付 `out`。`drive_divide` 等绘图提示不负责匹配节拍；
线程安全由实际 SPSC Buffer 提供，而不是由颜色或引脚编号提供。
水位由宿主通过内部统计通知更新，外部参数写入不能修改只读探针。

## 注意事项

- 节点必须归属于消费 Task，上游节点必须属于另一个 Task。
- 普通音频线不允许直接跨 Task；必须通过本组件形成明确同步边界。
- Runtime 的 Task 入口由调用方串行调度；并发宿主接入时 Ring Buffer 是唯一共享数据面。
- 跨域样本时间元数据尚未逐帧入队；消费者沿自身 Task 时间线处理。
- 配置是唯一接口事实源，`generated/` 用独立生成脚本更新；手写算法位于 `user/`。
