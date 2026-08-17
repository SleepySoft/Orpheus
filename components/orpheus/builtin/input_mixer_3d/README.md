# input_mixer_3d - 3D输入混音器

> Orpheus 专用扩展组件（L1），对应 Symphony SAS 的 InputMixer3D + DownmixToStereo 块。

## 功能

加权矩阵混音器。将 M 个输入通道按权重矩阵线性混合为 N 个输出通道，可选输出增益。权重矩阵通过 BULK 槽双缓冲直写，支持运行时热更新。

覆盖 Symphony SAS 的 InputMixer3D（5.1.4 加权）与 DownmixToStereo（8 通道下混立体声）两个块。

## 端口

| 端口 | 方向 | 类型 | 通道 |
|------|------|------|------|
| `in` | 输入 | audio f32 | param:input_channels |
| `out` | 输出 | audio f32 | param:output_channels |

## 参数

| 参数 | 类型 | 默认 | 范围 | 更新策略 | 说明 |
|------|------|------|------|----------|------|
| `input_channels` | int | 8 | [1, 32] | restart | 输入通道数（影响签名） |
| `output_channels` | int | 2 | [1, 32] | restart | 输出通道数（影响签名） |
| `gain_db` | float | 0.0 | [-96, 24] | smoothed | 输出增益（dB） |
| `weights` | string | 单位矩阵 | - | restart | 权重矩阵（逗号分隔，output×input 个） |

## BULK 槽

| 槽 ID | 类型 | 数量 | 说明 |
|-------|------|------|------|
| `weights` | float | 1024 | 权重矩阵 `32×32` 行优先，双缓冲 |

实际使用前 `output_channels × input_channels` 个 float，按行优先填充 `[out0_in0, out0_in1, ..., out1_in0, ...]`。行间距为 `IM3D_MAX_CHANNELS=32`（固定步长，便于 BULK 边界对齐）。双缓冲保证权重原子切换。

## 算法

```
prepare:
  weights 默认为单位矩阵（直通）
  若提供 weights 字符串则按行优先解析覆盖

process:
  for each frame n:
    for each output o:
      sum = sum_i(weights[o*MAX + i] * in[n*inCh + i])
      out[n*outCh + o] = sum * gain_linear
```

- `gain_linear = 10^(gain_db/20)`，`gain_db` 平滑更新时即时重算
- 矩阵按 `IM3D_MAX_CHANNELS=32` 固定行步长存储（BULK 边界对齐）

## 使用示例

```yaml
# 8 通道下混为立体声：L=in0+in2, R=in1+in3（其余忽略）
- id: downmix
  component: orpheus.builtin.input_mixer_3d
  params:
    input_channels: 8
    output_channels: 2
    gain_db: -3.0
    weights: "1,0,1,0,0,0,0,0,0,1,0,1,0,0,0,0"   # 2x8 = 16 个 float
```

## 源码映射

| Symphony SAS 源码 | 本组件 |
|---|---|
| `Model_1_1.c` PreAmp InputMixer3D（514 加权） | `input_mixer_3d` 权重矩阵子块 |
| DownmixToStereo `Weights_L_R[8]`（8ch->L/R） | `weights` 矩阵 output=2, input=8 |
| `InputMixer3dWeights_514[3]`（LFE/Lrs/Rrs 加权加法） | `weights` 矩阵对应行 |
| 运行时权重更新 | BULK `weights` 槽双缓冲直写 |

## 限制

- 最大 32x32（Symphony SAS 实际用 8x2 / 5.1.4，在范围内）
- 行步长固定 32（`IM3D_MAX_CHANNELS`），权重矩阵按行优先紧凑解析后展开到固定步长
- 无非线性/3D HRTF 处理（纯线性加权矩阵）
