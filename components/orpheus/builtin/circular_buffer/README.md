# orpheus.builtin.circular_buffer — 重叠音频分帧

## 功能

带历史重叠的流式分帧器（STFT 的"帧化"步骤）。每来一个输入块，组件把它拼在内部保留的 `frame_size − hop_size` 个历史样本之后，然后以 `hop_size` 为帧移、`frame_size` 为帧长，滑窗切出 `num_frames` 个重叠帧，**首尾相接展开**成一个大输出块：

```
out = [ 帧0 (frame_size) | 帧1 (frame_size) | ... | 帧N-1 (frame_size) ]
输出块长 = num_frames × frame_size（样本/通道）
```

分帧 + 重叠是 STFT 的前置工作：相邻帧共享 `frame_size − hop_size` 个样本，避免加窗后帧边缘信息丢失。本组件就是为 RNC TID5/TID6 STFT 分析链（`circular_buffer → window → rfft → spectral_reduce`）准备帧流而做的。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input，process=true | audio | 每次完整接收 `num_frames × hop_size` 帧，`channels` 通道 |
| out | output，drive_port_no=1 | audio | 展开帧流，块长 = `num_frames × frame_size`，通道数与输入相同 |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels` | int | 2 | 通道数，1~64；改变后需重新编译（affects_signature） |
| `frame_size` | int | 128 | 帧长，2~4096；restart_required，affects_signature |
| `hop_size` | int | 64 | 帧移（相邻帧起点间距），1~4096；restart_required，affects_signature |
| `num_frames` | int | 12 | 每块输出帧数，1~256；restart_required，affects_signature |

## 关键参数详解

### `frame_size` / `hop_size`：重叠率

- 重叠样本数 = `frame_size − hop_size`。典型 50% 重叠：frame=128, hop=64（TID5 配置）；或 frame=256, hop=128（TID6 配置）。
- 必须满足 `hop_size <= frame_size`；相等时不保留历史，大于帧长明确拒绝。
- 帧长通常等于下游 `rfft` 的 `fft_size`；hop 决定时间分辨率与块率。

### `num_frames`：显式载荷

每个输入块恰好推进 `num_frames × hop_size` 个新样本（例：hop=64 × 24帧 = 1536样本/调用）。
输入长度是接口声明，不取自Task/ctx处理量子；超长或不足明确报错，不截断、不补调。
下游必须声明 `num_frames × frame_size` 载荷，并使用相同帧结构解读展开布局。

## 注意事项

- 输出块远长于输入块（放大 `frame_size / hop_size` 倍）：这是**块长变化组件**，`affects_signature` 参数改了必须重新编译。
- 输出布局是"帧首尾相接"的展平流，通道仍按样本交错（`out[(f·frame+n)·ch + c]`）；下游 `window`（repeat 模式）和 `rfft`（fft_size=帧长）都按此约定解释。
- 启动时历史为全零：第一块输出的头部帧包含零填充的前缀，属正常预热。
- 4 个参数全部 restart_required + affects_signature；不支持运行时改帧结构。
- 保存历史后由算法显式交付1号out；写满、水位或声明规格不产生额外调用。
- 展开载荷不是连续时间样本流；Delivery保留本次新输入批次的时间锚点和标志，
  不能用展开槽位数推断真实采样推进。DISCONTINUITY不自动reset算法历史。
- 零处理量子不改变历史、不交付；下游失败不回滚已保存历史。

## 典型用法

```yaml
# TID5 STFT 链：12 通道、128 帧长、50% 重叠、每块 24 帧
- id: cb
  component: orpheus.builtin.circular_buffer
  params: { channels: 12, frame_size: 128, hop_size: 64, num_frames: 24 }
- id: fft
  component: orpheus.builtin.rfft
  params: { channels: 12, fft_size: 128, batches: 24, output_mode: power }
```

这里未包含分析窗；加入窗函数时，其输入/输出也必须声明相同展开载荷。
历史外部模型的tid5/tid6工程仍待整体迁移，不能据此宣称已完成模型对齐。

## 实时安全

- `process` 无内存分配、无锁、无 IO；历史与拼接 scratch 在 prepare 一次性分配。
- 不支持就地处理（supports_inplace=false，输入输出尺寸不同）。
- `reset` 只清零历史缓冲，等价于重新预热。
- 拒绝输入/输出及与State、历史、scratch的重叠；prepare分配失败清理并可重试，
  非法参数候选保留现有资源。
- [component.yaml](component.yaml) 为接口事实源，算法在 [user/algorithm.c](user/algorithm.c)，
  由独立 `scripts\generate_component.py` 生成壳，不再编译旧生产入口。
