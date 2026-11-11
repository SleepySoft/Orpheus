# input_mixer_3d - 矩阵混音与输出增益

> 通用矩阵混音组件；历史ID保留`input_mixer_3d`，但没有三维空间或HRTF处理。

## 功能

加权矩阵混音器。将 M 个输入通道按权重矩阵线性混合为 N 个输出通道，可选输出增益。权重矩阵通过 BULK 槽双缓冲直写，支持运行时热更新。

覆盖外部模型的 InputMixer3D（5.1.4 加权）与 DownmixToStereo（8 通道下混立体声）两个块。

## 端口

| 端口 | 方向 | 类型 | 通道 |
|------|------|------|------|
| `in` | 输入 | audio f32 | param:input_channels |
| `out` | 输出 | audio f32 | param:output_channels |

## 接口交付

落盘接口配置由独立 `scripts/generate_component.py` 校验并生成类型化壳，算法在 `user/algorithm.c`。
`in` 为唯一驱动入口，每次真实到达按帧、输出行、输入列顺序完成f32点积，
再乘共享输出增益并显式交付 `out(1)`；同拍调用分别处理，不等待会合或去重。
零帧不计算或交付；正帧须有完整输入与输出，拒绝原地、部分重叠和私有状态别名。
reset保留系数，增益每次实际调用从当前gain_db计算，按名称、ID及控制链修改都立即生效，
不依赖只有setter才更新的增益缓存。

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

`weights`配置字符串按紧凑的`output_channels × input_channels`排列，准备阶段展开到固定行步长32。
保留单位矩阵底图与前缀覆盖；空文本就是单位矩阵，合法多余尾项只校验、不写入。
原8×2默认CSV保持不变；其它尺寸需要单位矩阵时可显式配置空文本。
CSV有限且格式严格，不再截断4096字节以上的合法矩阵或把坏数值当作零。
直接写BULK槽时，第`o`行第`i`列位于`o*32+i`，不能把紧凑矩阵直接写入槽后当作多行矩阵。
槽始终是1024项、物理shape为32×32，与实际音频通道数无关；
允许前缀写入，超过1024项拒绝，写入影子区后到下一次Task/全局入口才切换。
双缓冲保证权重原子切换。与`matrix_mul`的紧凑行优先矩阵不同，本组件另有共享输出增益。

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

- `gain_linear = 10^(gain_db/20)`，每次真实输入使用当前值，不额外引入组件内部渐变
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

| 外部模型源码 | 本组件 |
|---|---|
| `Model_1_1.c` PreAmp InputMixer3D（514 加权） | `input_mixer_3d` 权重矩阵子块 |
| DownmixToStereo `Weights_L_R[8]`（8ch->L/R） | `weights` 矩阵 output=2, input=8 |
| `InputMixer3dWeights_514[3]`（LFE/Lrs/Rrs 加权加法） | `weights` 矩阵对应行 |
| 运行时权重更新 | BULK `weights` 槽双缓冲直写 |

## 限制

- 最大 32x32（参考模型实际用 8x2 / 5.1.4，在范围内）
- 行步长固定 32（`IM3D_MAX_CHANNELS`），权重矩阵按行优先紧凑解析后展开到固定步长
- 无非线性/3D HRTF 处理（纯线性加权矩阵）
