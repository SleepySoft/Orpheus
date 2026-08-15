# orpheus.builtin.downrate — 分频器

## 功能

**同时钟域分频（重缓冲）**：把 N 个常规块拼成一个 N×块长的"超块"，下游每 N 个块才执行一次、每次处理一整个超块。适合控制速率/分析速率的算法（RMS 分析、频谱、自适应侧链等）——它们不需要每块都跑，降频执行省 CPU。

工作原理（`src/downrate.c`）：每次 process 把当前输入块 `memcpy` 到输出缓冲的 `offset_frames` 处并累加偏移；写满 `frame_capacity`（= 输入块长 × factor）后偏移归零、`frame_count` 置满，下游这一拍拿到完整超块。

### 与 `resample` 的区别

| | downrate（本组件） | resample |
|---|---|---|
| 本质 | 抽块分频：数据原样重缓冲 | 重采样：N 个样本滑动平均成 1 个 |
| 输出采样率 | **不变**（仍等于 task 采样率） | `task:sample_rate / factor` |
| 输出块长 | 输入块长 × factor | 与输入块长相同 |
| 数据内容 | 逐样本无损保留 | 有抗混叠平均，样本数减为 1/N |
| 适用 | 分析/控制侧链降频执行 | 真正降低采样率的信号链 |

两者都通过 manifest 的 `scheduling.divisor: param:factor` 声明多速率：本节点每块都跑，其输出域（及下游）每 N 块触发一次（Runtime 按 `(counter+1) % divisor == 0` 相位触发）。

## 端口

| id | 方向 | 说明 |
|---|---|---|
| `in` | input | 音频输入，`channels` 通道，常规块长 |
| `out` | output | 音频输出，`channels` 通道，块长 = 输入块长 × `factor`（`block_size: in:block_size*param:factor`） |

## 参数

| 参数 | 类型 | 默认值 | 范围 | update_policy | 说明 |
|---|---|---|---|---|---|
| `factor` | int | 4 | 1~64 | restart_required（影响签名） | 分频比：下游每 N 块执行一次，超块 = N 个输入块 |
| `channels` | int | 2 | 1~32 | restart_required（影响签名） | 通道数 |

## 注意事项

- `factor` 决定调度相位和输出缓冲容量，改动必须重新编译。
- 本组件**不做任何滤波**，输出是输入的逐样本拼接；需要真正降采样（样本数减少）请用 `resample`。
- 输出有效数据以"写满一拍"为单位；`reset` 会把偏移归零重新对齐。
- 下游组件看到的是 N× 帧数的块，其 `process` 拿到的 `frame_count` 就是超块长度。

## 典型用法

主链 48 kHz / 128 帧每 2.67 ms 一块；接 `downrate(factor=8)` 后，分析侧链每 ~21 ms 执行一次、每次处理 1024 帧：

```
device_in ──► 主链处理 ──► device_out
     └──► downrate(factor=8) ──► rfft / psd / 自适应分析侧链
```

## 实时安全

process 内只有一次整块 `memcpy` 与计数器更新，无堆分配、无锁、无 IO；`realtime_safe: true`。
