# orpheus.builtin.rnc_mimo_nlms - 多参考多输出NLMS

## 功能

通用多参考、多输出时域NLMS核心。每个输出由全部参考通道的FIR权值共同生成，并使用上游提供的filtered error更新权值。
当前容量上限为12路参考、8路输出、125抽头；可在范围内配置尺寸、步长及初始权值。
以下ASM RNC `AdaptFilter`映射说明实现来源，不要求通用工程采用同一传感器、扬声器或TOP表。

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
| `leakage` | AdaptFilter `Leakage` | 每次正帧真实调用轮转处理一条 reference/output FIR，先泄漏再计算，不按Task拍数推导。 |
| `eps` | `normx + 1e-5` | 归一化正则项，模型为 `1e-5`。 |
| `initial_weights` | `NlmsAdaptiveFilterCoeffsInit[12000]` | 逗号分隔初始权值。大型模型建议用提取脚本生成。 |

运行期暴露 `weights`（双 bank）和 `step_sizes` 两个 BULK 槽，
实际数量分别为 `output_channels*reference_channels*filter_length` 和 `output_channels`，
物理容量仍为 12000 和 8。filter_length也影响BULK签名，修改需重启。
初始CSV只允许有限f32数值、逗号或空白分隔；允许前缀初始化，剩余补零，
空值代表全零，拒绝垃圾字符、NaN/Inf、溢出和多余项，不再跳过坏字符。
step_sizes默认空值与原八项零在数值上等价，也适用于缩小输出数量的配置。
initial_weights显式预留262144字节（含NUL），可容纳12000项紧凑f32十进制值；
大型表建议使用九位有效数字，不能静默截断。扩大的通用字符串上限不改变BULK元素容量。

## 端口

- `ref`：唯一驱动入口，通道数=`reference_channels`，每次实际到达都计算。
- `filtered_error`：只供数据，不驱动；上游须先准备。
  每个输出对应一项外部误差，不在内部减去预测输出。
- `out`：驱动编号1。全部历史/权值/轮转位置提交后显式emit，下游失败不回滚。
  两输入和输出必须绑定；零帧不推进，拒绝短输入、容量不足、私有状态或输入/输出别名。

归一化以eps为初值，按参考/抽头顺序累加f32能量；
逐样本、逐输出计算旧权重的卷积，同时按外部误差更新权重，保留原f32顺序。
leakage小于1时，轮转先遍历输出，再遍历参考；等于1时不推进轮转游标。
即使步长为零，配置的轮转泄漏仍生效，不套用nlms“缺误差冻结”的语义。

## 边界

本组件只负责生成代码 `<S724>/AdaptFilter` 的核心卷积、归一化更新和轮转 leakage。系数历史切换、发散恢复、FFT 频带清理及 Wiener filtered-error 路径属于外围状态机，需在图上由其他组件连接，不在组件内部隐式实现。

调用 `reset` 会恢复 `initial_weights` 并清空参考历史；运行期写入 `weights` BULK 只改变当前自适应权值，不覆盖 reset 基线。
reset同时清泄漏游标，保留运行期step_sizes；重新prepare重新加载配置。

## 生成与验证

先保存唯一接口声明，再调用独立`scripts/generate_component.py`校验/生成；
壳与算法分离，无旧ABI头依赖。
`rnc_mimo_smoke`以非环形参考逐位验证3/1/2帧调用、15次泄漏轮转、
错误后继续、参数事务、私有别名/容量、零帧、reset和真实BULK偏移；
原有两组固定数值冒烟也保留并接入生成壳。
`test_rnc_mimo_interfaces.py`覆盖四图/四入口、最大维度和超过64KB的初始化，
并单独验证双bank开启/关闭时的真实权值与步长更新。
