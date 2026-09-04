# orpheus.builtin.rnc_mimo_nlms - RNC 多参考 NLMS

## 功能

实现 BAF ASM RNC `AdaptFilter` 的多参考、多扬声器时域 NLMS 核心。每个输出都由全部参考通道的 FIR 权值共同生成，并使用上游提供的 filtered error 更新权值。

权值索引为：

```text
((output * reference_channels + reference) * filter_length + tap)
```

模型默认维度为 `8 × 12 × 125 = 12000`，与 `Model_Target_Rnc_p15_b5.NlmsAdaptiveFilterCoeffsInit` 一致。

## 参数

| 参数 | 模型对应 | 说明 |
|---|---|---|
| `reference_channels` | `NumActiveAccelChannels` | 参考通道数，模型为 12。 |
| `output_channels` | `NumActiveSpeakers` | 输出/扬声器数，模型为 8。 |
| `filter_length` | `AdaptiveFilterLength` | 每条参考到输出路径的 taps，模型为 125。 |
| `step_sizes` | `NlmsStepSize[8]` | 逗号分隔的逐输出步长；当前参考 TOP 默认全 0。 |
| `leakage` | AdaptFilter `Leakage` | 每图块轮转处理一条 reference/output FIR，避免一次处理全部 12000 权值。 |
| `eps` | `normx + 1e-5` | 归一化正则项，模型为 `1e-5`。 |
| `initial_weights` | `NlmsAdaptiveFilterCoeffsInit[12000]` | 逗号分隔初始权值。大型模型建议用提取脚本生成。 |

运行期还暴露 `weights`（12000 float，双 bank）和 `step_sizes`（8 float）两个 BULK 槽。

## 端口

- `ref`：多路加速度计参考，通道数=`reference_channels`。
- `filtered_error`：每个输出对应的 filtered error。完整 BAF 模型会先通过 Mic-to-Speaker 和 Speaker-to-Speaker Wiener FIR 计算此信号。
- `out`：多输出自适应 FIR 输出。

## 边界

本组件只负责生成代码 `<S724>/AdaptFilter` 的核心卷积、归一化更新和轮转 leakage。系数历史切换、发散恢复、FFT 频带清理及 Wiener filtered-error 路径属于外围状态机，需在图上由其他组件连接，不在组件内部隐式实现。

调用 `reset` 会恢复 `initial_weights` 并清空参考历史；运行期写入 `weights` BULK 只改变当前自适应权值，不覆盖 reset 基线。
