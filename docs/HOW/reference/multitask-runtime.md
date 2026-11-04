# 多 Task Runtime 与异步桥

## 目标

工程中的 `tasks` 不再只是端口参数上下文。动态 Runtime 与生成工程提供一致的 Task 入口：
Task 只启动直接入口组件，后续由组件通过 ABI v6 续接接口调用其直接下游。
具体契约见[组件链式调用](task-driven-execution-design.md#0-当前实现task-启动入口组件调用下游)。

## Plan 契约

`plan.tasks[]` 每项包含：

- `id/name/sample_rate/block_size/priority`
- `nodes/execution_order`
- `entry_nodes`：Task 可直接启动的入口；全图 `call_graph` 保存组件的直接调用连接
- `schedule.tick`：该 Task 在图采样率时间轴上的触发量子
- `schedule.periods`：节点相对该 Task tick 的触发周期

`execution_order` 仅用于编译分析、内存布局及展示，不参与运行期逐节点分派。
`schedule` 用于宿主推进和入口节拍，不在下游组件处执行周期门控。

## 执行入口

动态 Runtime：

```cpp
runtime.process_task("producer", producer_frames);
runtime.process_task("consumer", consumer_frames);
```

生成工程：

```c
orpheus_generated_process_task_producer(producer_frames);
orpheus_generated_process_task_consumer(consumer_frames);
```

文件宿主支持重复的 `--task <id> <blocks>`。每个 Task 有独立入口计数器，
每条分频连接有自己的调用计数器。组件不续接时，下游不会由 Task 代为调用。

## 跨 Task 规则

普通音频边禁止直接跨 Task。跨 Task 边的目标必须是：

- `async_bridge`：一般异步任务桥；或
- `rate_sync`：历史多速率合流同步点。

编译器将跨 Task 输入边标记为 `task_bridge`，记录生产块长、消费块长和 Ring Buffer 容量。`capacity_frames=0` 时，容量为上下游块长 LCM 的两倍。

## SPSC 数据面

- 每条 task bridge 独占固定容量 Ring Buffer，处理阶段不分配内存。
- 生产者只推进 `write_pos`，消费者只推进 `read_pos`。
- 队列满时丢弃当前生产块并累计 `overruns`。
- 数据不足时读取已有帧、尾部补零并累计 `underruns`。
- `level_frames`、`underruns`、`overruns` 通过 `async_bridge` PROBE 槽读回。
- C++ Runtime 使用 `std::atomic<uint64_t>`；生成 C 在 MSVC 使用 `Interlocked*64`，其他 C11 工具链使用 `_Atomic uint64_t`。

## 调度与并发边界

当前内置文件宿主和 UI 仍使用全局确定性调度入口。平台宿主可调用独立 Task 入口，但同一 Task 只能有一个执行者。SPSC 音频数据面允许生产/消费 Task 分线程；参数控制和 BULK 更新仍遵循现有“单控制写者、块边界提交”约束，平台不得从多个 Task 同时提交同一控制槽。

## 验证

- 编译期拒绝普通跨 Task 直连。
- 24 帧生产 Task 与 32 帧消费 Task 自动生成 192 帧 Ring Buffer。
- 50 个 96 帧周期覆盖多次游标回绕。
- 动态 Runtime 与生成 C 工程输出 WAV 逐字节一致且非静音。
- UI 打开并保存工程后保持节点 Task 归属。
