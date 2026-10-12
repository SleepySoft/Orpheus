# A.POST_PROCESS 分层分析

状态：阶段归属、默认 TOP 参数和数组布局已提取；运行期校准、RTC 状态迁移和 golden 尚未完成。

## 用途

PostProcess 不只是输出 EQ。它负责把 PreAmp 提供的 32 路候选信号组织成 22 路物理输出，同时完成保护、软削波、静音、校准、频响和传播时间补偿。

## 两层处理顺序

内部 22 路处理：

```text
gain ramp -> limiter -> PostEQ -> internal soft clip -> RMDL mute
```

外层路由与输出处理：

```text
32 路候选/内部结果/辅助源
  -> ASD main + aux router
  -> all mute
  -> output soft clip
  -> 每通道 output calibration
  -> FreqComp IIR
  -> 每通道 tunable delay
  -> 22 路主输出
```

两级 soft clip 使用不同默认阈值：内部级为较低阈值的保护曲线，输出级默认阈值更高。它们不能折叠为一个节点，否则会改变 limiter/EQ、路由和校准之间的非线性位置。

## 路由与运行期状态

- `ASDRouterMainSelect[22]` 默认选择 1..22；RTC 项最高位为 1 时保留 TOP，否则使用 RTC 值。
- `AuxSelect[22]` 和 enable/gain 允许把辅助输入累加到主通道，不是互斥 selector。
- `OutputCalVals[22]` 来自运行期状态/RTC，不在本轮 TOP 默认表中，仍标记 pending。
- limiter 每通道保存增益状态并上报 active bitmask；两套 high/low 参数由运行期 param set 选择。
- PostEQ/FreqComp 均有运行期 reset/shutdown 状态，不能仅复制默认系数。

## 已落盘证据

`evidence/postprocess-defaults.json` 保存主/辅路由、两套 limiter 数组、PostEQ/FreqComp 系数与 stage count、两级 soft clip 参数、mute ramp、test matrix 和 22 路 delay。大数组保留值与规范化 hash，便于后续按模块拆出独立 golden。
