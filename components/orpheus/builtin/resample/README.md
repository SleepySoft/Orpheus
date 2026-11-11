# orpheus.builtin.resample — 整数倍降采样

## 两个独立入口

每 `factor` 个输入样本生成一个输出样本，输出采样率为输入采样率除以 `factor`。
`mode=0` 分组平均；`mode=1` 逐位保留组内 `phase` 指定的样本，不作抗混叠滤波。
分组累加器/选点相位跨写入调用连续，不能按输入块边界重新分组。

- `in`：`process: true`，每次处理 `input_frames` 帧，结果写入私有存储。
  **写满不交付、不调用下游**。
- `tick`：纯触发、`process: true`，独立读取完整 `output_frames` 帧，
  消费水位后显式交付 1 号 `out`。
- `out`：`output_frames` 帧，采样率 `in:sample_rate/param:factor`，
  `drive_port_no: 1`。

调用顺序与读取频率由上游及真实触发链安排，`factor` 不再声明调度 divisor。
探针水位不是驱动事件，框架不补调、等待、匹配频率或按 Task 去重。

## 参数

| 接口 | 默认值 | 范围 | 说明 |
|---|---|---|---|
| `factor` | 2 | 2～64 | 输入样本数与输出样本数之比 |
| `channels` | 2 | 1～32 | 交错 f32 通道数 |
| `input_frames` | 32 | 1～65536 | 每次写入的完整载荷长度 |
| `output_frames` | 32 | 1～65536 | 每次读取的完整载荷长度 |
| `mode` | 0 | 0～1 | 0平均，1纯选点 |
| `phase` | 0 | 0～63且小于factor | 纯选点的0基组内位置 |

以上均为 `restart_required` 参数，不能运行期修改。
必须满足 `output_frames * factor` 可被 `input_frames` 整除，
保证若干次完整写入能恰好形成一个完整读取批次；
不要求 `input_frames` 本身能被 `factor` 整除。
例如每次写7帧、factor=3、读取14帧，需要6次写入，分组状态跨调用保留。

只读探针 `available_frames` 表示已形成的可读输出帧数；
`underflows/overflows` 为欠载/未读覆盖饱和计数。
探针初值必须为零，不能通过 setter 修改。

## 时间与错误

- 第一小块的输入帧位置必须对齐 `factor`，输出绝对帧位置为输入起点除以 `factor`，
  epoch保留，质量标志聚合本批输入；读取tick的时间不能冒充数据时间。
- 批次内要求输入时间连续、epoch一致；中途 `DISCONTINUITY`、
  不完整批次的 `EOS` 及时间加法溢出均在改变状态前拒绝。
- 欠载及未读覆盖明确报错，保留已有数据，不补零；
  载荷不匹配及与State/私有存储重叠也明确拒绝。
- 消费先于下游调用推进，下游失败不重放；零帧调用不推进、不交付。
  `reset` 清除分组状态、水位、探针与时间。
- 显式载荷不覆盖 `ctx.frame_count` 处理量子；
  下游应声明 `output_frames` 载荷，用实际Delivery的局部时间处理数据，
  不从Task拍数推断低速域的调用时间。

## 与组块的区别

`downrate` 原样拼接音频，采样率不变；
本组件将N个样本平均或选点成1个，真正改变采样率。
平均模式只是盒式低通，阻带抑制有限；选点模式完全不抗混叠，
高要求场合应在上游使用更陡的低通，或采用有理倍FIR转换。

## 实际工程

[WAV降采样示例](../../../../examples/wav_resample.yaml)：

```text
wav_in.out ───────────────► resample.in
wav_in.tick ─► trigger_divider（2倍、相位1）─► resample.tick
resample.out ─────────────► wav_out（24kHz）
```

WAV源先完成音频发布子树，再发出纯触发；
第二次发布后读取128帧低速音频。这里的分频由触发分频器算法实际执行，
不是由采样率声明、水位或绘图注记自动产生。

## 生成与资源

[component.yaml](component.yaml) 是接口事实源，独立生成脚本生成 `generated/`，
手写算法位于 [user/algorithm.c](user/algorithm.c)。
私有输出存储只在 `prepare` 分配、`destroy` 释放；
非法候选保留资源，分配失败清理后可重试。
实时入口只有有界累加/选点、拷贝、时间检查与计数器更新，无分配、锁或 IO。
