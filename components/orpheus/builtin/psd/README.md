# orpheus.builtin.psd — 功率谱估计

## 功能

对输入音频逐块做 FFT，计算每个通道的功率谱密度（PSD），再把通道 0 的原始谱重采样为覆盖 Nyquist 频段的 1023 个显示 bins，通过 `spectrum` 探针以二进制 float 数组输出。音频本身原样直通，属于监控/分析类组件。

常用于频谱可视化、啸叫检测、频率响应分析，或作为 Audiopilot 等算法的前端频谱提取。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels` | int | 2 | 输入/输出通道数，改变后需重新编译。 |
| `smoothing` | int | 8 | 指数平滑的等效块数，越大越平滑。 |
| `spectrum` | float[1023] | 0.0 | 通道 0 的平滑功率谱（readback，只读）。 |

### `spectrum` 的格式

返回 1023 个 float，由原始 `block_size/2` 点幅度谱线性重采样而来，其中块长必须是 2 的幂。

- 索引 0 对应直流分量。
- 索引 `k` 对应频率 `k * Nyquist / 1022`。
- 1023 个 float 加 8 字节消息头恰好符合 Bridge 单帧上限。

### `smoothing` 的真实含义

同 `coherence_matrix`，使用一阶 EMA：

```
mag[k] += alpha * (instant_mag[k] - mag[k])
alpha = 1 / smoothing
```

### 幅度计算

内部公式：

```
instant_mag = 2 * sqrt(re^2 + im^2) / N
```

结果大致对应于各频率分量的幅度谱。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，与输入完全一致。

## 注意事项

- 与 `coherence_matrix` 一样，要求 `frame_count` 是 2 的幂，否则 spectrum 不再更新。
- 只上报通道 0 的频谱；如需多通道频谱，请对每个通道分别使用 `psd`。
- prepare 时会分配 FFT 缓冲区，不在 process 中分配。

## 典型用法

```
mic (1ch) ──► psd ──► null_sink
```

把麦克风信号接入 `psd`，UI 读取 `spectrum` 即可画出实时频谱曲线。
