# orpheus.builtin.resample — 降采样器

## 功能

**整数倍降采样（N:1，滑动平均抗混叠）**：输入每 N 个样本累加取平均输出 1 个样本，输出采样率降为 `task_sample_rate / N`，下游按分频调度执行。

工作原理（`src/resample.c`）：每通道维护一个累加器 `acc[c]` 与计数 `n`；每读满 N 帧就把 `acc[c]/N` 写入输出缓冲并清零累加器。输出缓冲写满 `frame_capacity` 时 `frame_count` 置满、`write_pos` 回卷。N 点滑动平均是最简抗混叠滤波（先低通再抽取），比直接丢样本的混叠小得多，但阻带衰减有限，对音质要求高的场合应接更陡的抗混叠滤波器。

### 与 `downrate` 的区别

| | resample（本组件） | downrate |
|---|---|---|
| 本质 | 重采样：N 个样本平均成 1 个 | 抽块分频：数据原样重缓冲 |
| 输出采样率 | `task:sample_rate / factor`（端口签名声明） | 不变 |
| 输出块长 | 与输入块长相同 | 输入块长 × factor |
| 数据内容 | 样本数减为 1/N，有平均滤波 | 逐样本无损保留 |
| 适用 | 真正降低采样率的信号链 | 分析/控制侧链降频执行 |

两者都通过 `scheduling.divisor: param:factor` 声明多速率：下游每 N 块触发一次。

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

## 注意事项

- `factor` 最小为 2；`factor = 1`（不降采样）的场景直接用直通。
- 抗混叠只是 N 点滑动平均，对接近原奈奎斯特频率的强成分保护有限；高要求场合请在前面串 `biquad`/`fir` 低通。
- 累加器跨块连续（reset 清零），块边界不会断相；输出帧在输出缓冲内按 `write_pos` 顺序排布，下游按 `frame_count` 消费即可。
- `sample_rate_independent: false`：签名里的采样率换算依赖 task 采样率，换 task 采样率需重新编译。

## 典型用法

48 kHz 采集 → 2:1 降采样 → 24 kHz 写文件（见 `examples/wav_resample.yaml`）：

```
wav_in(48k) ──► resample(factor=2) ──► wav_out(24k, sample_rate=24000)
```

## 实时安全

process 内仅逐样本累加/除法，无堆分配、无锁、无 IO；`realtime_safe: true`。
