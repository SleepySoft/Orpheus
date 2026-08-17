# orpheus.builtin.circular_buffer — 循环缓冲分帧

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
| in | input | audio | 输入音频，`channels` 通道 |
| out | output | audio | 展开帧流，块长 = `num_frames × frame_size`，通道数与输入相同 |

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
- 历史缓冲只保留 `frame_size − hop_size` 个样本（hop > frame 时历史长为 0，帧间有间隙）。
- 帧长通常等于下游 `rfft` 的 `fft_size`；hop 决定时间分辨率与块率。

### `num_frames`：输入块长的隐式约定

每个输入块应恰好推进 `num_frames × hop_size` 个新样本，即**上游块长应等于 `num_frames × hop_size`**（例：hop=64 × 24 帧 = 1536 样本/块）。组件内部 `max_input_frames` 取自配置的块长，超长输入会被截断；不足则由历史+现有样本拼帧，帧内容会包含旧数据。下游组件（如 `spectral_reduce`）用同样的 `num_frames` 参数来反解这个展开布局——三者参数必须配套。

## 注意事项

- 输出块远长于输入块（放大 `frame_size / hop_size` 倍）：这是**块长变化组件**，`affects_signature` 参数改了必须重新编译。
- 输出布局是"帧首尾相接"的展平流，通道仍按样本交错（`out[(f·frame+n)·ch + c]`）；下游 `window`（repeat 模式）和 `rfft`（fft_size=帧长）都按此约定解释。
- 启动时历史为全零：第一块输出的头部帧包含零填充的前缀，属正常预热。
- 4 个参数全部 restart_required + affects_signature；不支持运行时改帧结构。
- 输入块长超过 `max_input_frames` 时会被静默截断，帧流仍然按 num_frames 输出。

## 典型用法

```yaml
# TID5 STFT 链：12 通道、128 帧长、50% 重叠、每块 24 帧
- id: cb
  component: orpheus.builtin.circular_buffer
  params: { channels: 12, frame_size: 128, hop_size: 64, num_frames: 24 }
- id: win
  component: orpheus.builtin.window
  params: { channels: 12, window_size: 128, mode: repeat, coefficients: "..." }
- id: fft
  component: orpheus.builtin.rfft
  params: { channels: 12, fft_size: 128, output_mode: power }
```

（完整工程见 `examples/symphony_asm_ehc_rnc.yaml` 的 tid5/tid6 任务。）

## 实时安全

- `process` 无内存分配、无锁、无 IO；历史与拼接 scratch 在 prepare 一次性分配。
- 不支持就地处理（supports_inplace=false，输入输出尺寸不同）。
- `reset` 只清零历史缓冲，等价于重新预热。
