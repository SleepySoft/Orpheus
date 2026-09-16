# orpheus.builtin.sport_tdm_out — SPORT/TDM 输出

`sport_tdm_out` 是 Orpheus 图输出到外部 SPORT/TDM DMA 数据块的音频边界。组件本身不启动 SPORT 传输，也不配置 SPORT、DAI/SRU、PCG 或 DMA；它把图内交错 `f32` 结果交给生成的 adapter，再由 adapter 完成格式转换和 slot scatter。

## 适用层级

当前实现和验证目标是 `adsp21593`（ADSP-21593）。该实现模式对应 ADI SHARC/SHARC+ 21xxx 的 full-duplex SPORT、TDM slot、外设 DMA 与 DAI/SRU 边界；不能据此宣称所有 ADI DSP 都已支持。

## 端口

| 端口 | 方向 | 类型 | 格式 | 通道 |
| --- | --- | --- | --- | --- |
| `in` | 输入 | audio | `f32` | `channels` |

输入数据必须是交错布局，每个 Task 块包含 `frames * channels` 个 `float`。

## 参数

| 参数 | 类型 | 范围 | 默认值 | 更新策略 |
| --- | --- | --- | --- | --- |
| `channels` | int | 1–64 | 2 | restart_required |
| `sample_rate` | int | 1000–384000 | 48000 | restart_required |

参数数量刻意少，因为这里只有两项属于图边界签名：逻辑通道数和采样率。物理 SPORT resource、TDM slot 总数、slot 到逻辑通道的映射、定点格式在工程顶层 `sport_bindings` 中声明；块长、时钟域和触发组在顶层 Task/调度契约中声明。编译器据此静态展开 scatter 和格式转换。

## 状态

| 字段 | 说明 |
| --- | --- |
| `channels` / `sample_rate` | prepare 时写入的图签名 |
| `dst` | adapter 提供的目标 slot 块指针 |
| `dst_capacity` | 目标块可容纳的帧数 |

## 处理行为

若 `dst` 非空且 `dst_capacity >= frame_count`，组件把输入块拷贝到 `dst`；生成 adapter 会在图 Task 后读取 `dst` 并完成定点编码和 slot scatter。若 `dst` 为空或容量不足，组件静默丢弃本块。当前没有 overrun/drop 探针；手写集成时必须保证目标容量至少等于 Task block size。

## 生成代码关系

代码生成器会为节点生成：

```c
extern float g_sport_out_<node>[];
SportTdmOutState* orpheus_sport_tdm_out_state_<node>(void);
```

生成的 SPORT wrapper 先调用图 Task，把输出写入 `g_sport_out_<node>`；随后按 `sport_bindings` 把逻辑通道转换为 `q1_31`、`s24_left_in_s32`、`s24_right_in_s32` 或直接写 `f32`，scatter 到对应 DMA TX slot。

## DSP 集成方法

1. 在工程中声明 `target: adsp21593`、时钟域、触发组、Task、`sport_bindings`，并把该组件放入对应 Task。
2. 使用 `orpheus_core.cli generate ... --target adsp21593` 生成独立 C 工程。
3. 把生成工程源码和组件源码加入 CCES 工程，保持 C11 编译选项。
4. 板级代码初始化 DAI/SRU routing、PCG/codec 时钟、SPORT 协议、TDM slot 宽度、DMA descriptor 和中断。
5. 在 master/follower TX buffer 就绪后，构造 `OrpheusSportIo_<task>` 与 `OrpheusExternalTriggerContext`，调用 `orpheus_graph_process_task_<task>_sport()`。
6. wrapper 返回后按板级 DMA 要求 flush/invalidate cache，并让 DMA 在当前 buffer 消费完成后再切换 bank。

若多个 SPORT TX 是同一 trigger group 的 follower，板级 adapter 必须收齐同一 sequence/相位的 buffer 后再调用生成 Task；生成器不会自动生成跨 callback barrier。

## 实时安全与限制

- `process()` 只执行一次拷贝，没有动态分配、锁、文件/网络 IO或日志。
- 参数会改变图签名，必须重新编译/重启，不能实时热改。
- 目标容量不足会静默丢块；生产集成应保证容量匹配，必要时增加板级探针。
- 组件不配置硬件时钟、引脚路由、DMA descriptor 或中断号。
