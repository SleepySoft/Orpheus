# iir_bank - N级IIR滤波器组

> Orpheus 专用扩展组件（L1），对应 BAF SAS 的 pooliir / GLXP IIR 加速器（软件近似）。

## 功能

可配置级数的级联双二阶（biquad）IIR 滤波器组。系数通过 BULK 槽连续直写，支持运行时热更新。是 pooliir 硬件加速器的软件近似实现。

## 端口

| 端口 | 方向 | 类型 | 通道 |
|------|------|------|------|
| `in` | 输入 | audio f32 | param:channels |
| `out` | 输出 | audio f32 | param:channels |

## 参数

| 参数 | 类型 | 默认 | 范围 | 更新策略 | 说明 |
|------|------|------|------|----------|------|
| `channels` | int | 2 | [1, 32] | restart | 通道数（影响签名） |
| `num_stages` | int | 4 | [1, 16] | restart | 级联级数 |
| `coefs` | string | "1,0,0,0,0" | - | restart | 系数（逗号分隔，5×num_stages 个） |

## BULK 槽

| 槽 ID | 类型 | 数量 | 说明 |
|-------|------|------|------|
| `coefs` | float | 80 | 连续系数 `[b0,b1,b2,a1,a2] × 16级`，双缓冲 |

实际使用前 `5 × num_stages` 个 float，余下忽略。双缓冲机制保证系数原子切换。

## 算法

每级为 Direct Form 双二阶：

```
y = b0*x + b1*z1 + b2*z2 - a1*z1 - a2*z2
z2 = z1
z1 = y
```

级联：`x -> stage[0] -> stage[1] -> ... -> stage[N-1] -> out`

每通道独立维护 z1/z2 状态。

## 使用示例

```yaml
- id: eq
  component: orpheus.builtin.iir_bank
  params:
    channels: 22
    num_stages: 13
    coefs: "1,0,0,0,0,1,0,0,0,0,..."   # 5×13=65 个 float
```

## 源码映射

| BAF SAS 源码 | 本组件 |
|---|---|
| pooliir GLXP IIR 加速器（workMem 1104, 22ch, 13stages） | iir_bank 22ch, 13 stages（软件级联） |
| `pooliirAccelerator.h` `pooliirAccel` CRL 替换 | `ib_process()` 级联循环 |
| SAS_PeripheralEq / PostEQ / HoligramIir | 均可用 iir_bank 配置不同级数和系数 |
| 系数来源：MixEqpooliirCoeffs[484]、OutputEQpooliirCoeffs 等 | BULK `coefs` 槽直写 |

## 限制

- 软件实现，无 GLXP 硬件加速（性能不同，功能等价）
- 最大 16 级、32 通道（pooliir 可达 13 级 × 22 通道，在范围内）
