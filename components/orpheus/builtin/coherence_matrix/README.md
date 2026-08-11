# orpheus.builtin.coherence_matrix — 相干矩阵

## 功能

对多路输入信号逐块做 FFT，并估计通道间的平均相干（magnitude-squared coherence），输出一个 `channels × channels` 相干矩阵，以及非对角元素的平均历史曲线。音频本身原样直通，不改变声音。

常用于分析麦克风阵列、扬声器阵列或 Audiopilot 等算法中“哪些通道在同步发声”。例如 10 路麦克风同时拾取同一声源时，相干矩阵的非对角元素会接近 1；如果各路噪声独立，则接近 0。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels` | int | 2 | 输入/输出通道数，范围 2~10。改变后需重新编译。 |
| `smoothing` | int | 8 | 指数平滑的等效块数。越大曲线越平滑、响应越慢。 |
| `coherence` | string | "{}" | 当前相干矩阵 JSON（readback，只读）。 |
| `history` | string | "[]" | 非对角平均相干历史（readback，只读）。 |

### `smoothing` 的真实含义

内部使用一阶指数滑动平均（EMA）：

```
new_value = old_value + alpha * (instant - old_value)
alpha = 1 / smoothing
```

- `smoothing = 1`：最快响应，但波动大。
- `smoothing = 32`：非常平滑，但突变会被拖长。

### `coherence` JSON 结构

返回格式示例：

```json
{"n":4,"bins":16,"matrix":[1,0.9,0.8,0.1,0.9,1,...]}
```

`matrix` 按行优先排列，共 `n × n` 个元素，对角线恒为 1（自己跟自己完全相干）。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，与输入完全一致（监控类组件）。

## 注意事项

- 该组件要求 `frame_count` 是 2 的幂，且 `frame_count / 2 == block_size / 2`。因此只有块长为 2 的幂时才会正常更新；否则音频仍直通，但相干矩阵不再刷新。
- 内部使用基 2 FFT，复杂度为 `O(channels × N log N)`，不适合在超小block上高频调用。
- 实时安全，但 prepare 时会 `calloc` 内部 FFT 缓冲区；这是允许的，因为不在 process 中分配。

## 典型用法

```
mic_array (10ch) ──► coherence_matrix ──► null_sink
```

把麦克风阵列信号接入 `coherence_matrix`，然后在 UI 或 RT host 中读取 `coherence` 探针，即可观察各路信号的相关性，用于判断噪声源方向或音乐/噪声分离效果。
