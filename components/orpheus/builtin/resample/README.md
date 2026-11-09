# orpheus.builtin.resample — 整数倍降采样

## 功能

**整数倍降采样（N:1）**：每N个输入样本输出一个样本，输出采样率为输入采样率除以N。
`mode=0`对每组N个样本取平均；`mode=1`只保留组内`phase`指定的样本，不作抗混叠滤波。

平均模式每通道维护累加器`acc[c]`，每N帧写入`acc[c]/N`并清零；选点模式保存组内指定相位的值。
计数和输出写游标跨调用连续，输出缓冲填满后发布完整块。
分组平均相当于盒式低通后抽取，阻带抑制有限；高要求场合应使用更陡的外置低通或有理倍FIR转换。

### 与 `downrate` 的区别

| | resample（本组件） | downrate |
|---|---|---|
| 本质 | N个样本平均或选点成1个 | 音频组块：样本原样拼接 |
| 输出采样率 | 输入采样率 / factor | 不变 |
| 输出块长 | 与输入块长相同 | 输入块长 × factor |
| 数据内容 | 样本数减为1/N；平均模式有盒式滤波，选点模式没有 | 逐样本无损保留 |
| 适用 | 真正降低采样率的信号链 | 分析/控制侧链降频执行 |

采样率、块长和驱动周期是不同概念。调用入口须按工程的供需节拍配置，
不能把`downrate`的拼块或缓冲写满理解为它创建定时器、自动分频或自动启动消费者。

## 端口

| id | 方向 | 说明 |
|---|---|---|
| `in` | input | 音频输入，`channels` 通道，task 采样率 |
| `out` | output | 音频输出，`channels` 通道，采样率 = `task:sample_rate / factor` |

## 参数

| 参数 | 类型 | 默认值 | 范围 | update_policy | 说明 |
|---|---|---|---|---|---|
| `factor` | int | 2 | 2~64 | restart_required（影响签名） | 抽取比：N 个输入样本 → 1 个输出样本 |
| `channels` | int | 2 | 1~32 | restart_required（影响签名） | 通道数（内部累加器上限 `RESAMPLE_MAX_CHANNELS = 32`） |
| `mode` | int | 0 | 0~1 | restart_required | 0分组平均，1纯选点 |
| `phase` | int | 0 | 0~63，且小于factor | restart_required | 纯选点模式的0基组内位置 |

## 注意事项

- `factor` 最小为 2；`factor = 1`（不降采样）的场景直接用直通。
- 平均模式的盒式低通阻带抑制有限；选点模式完全不作抗混叠，高要求场合请在前面串`biquad`/`fir`低通。
- 累加器跨块连续（reset 清零），块边界不会断相；输出帧在输出缓冲内按 `write_pos` 顺序排布，下游按 `frame_count` 消费即可。
- `sample_rate_independent: false`：签名里的采样率换算依赖 task 采样率，换 task 采样率需重新编译。

## 典型用法

48 kHz 采集 → 2:1 降采样 → 24 kHz 写文件（见 `examples/wav_resample.yaml`）：

```
wav_in(48k) ──► resample(factor=2) ──► wav_out(24k, sample_rate=24000)
```

## 实时安全

process 内仅逐样本累加/除法，无堆分配、无锁、无 IO；`realtime_safe: true`。
