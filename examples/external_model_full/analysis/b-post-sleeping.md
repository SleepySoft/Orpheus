# B.POST.SLEEPING 蒸馏记录

> 状态：已验证并替换 placeholder
>
> 实现：标准路由 composite + `orpheus.builtin.sleeping_beauty`

## 边界

源码 `MedusaPart5DeciRatePostHoligramTID3` 的 `MatrixConcatenate` 顺序为：

1. Peripheral HEQ 10ch（offset 0）；
2. Overhead HEQ 4ch（offset 320，即 channel 10）；
3. Headrest HEQ 40ch（offset 448，即 channel 14）。

`sleepingBeautyProcess` 输入/输出均为 54ch × 32 frames。处理后源码只切出前 14ch 与后 40ch；Row1/Row2 不是 SleepingBeauty 输出，而是后续 `MedusaHeadrestProcessing` 对 headrest40 的前/后 20ch 分组。因此新增已验证的 `B.POST.HEADREST_SPLIT`，不再让 SB 伪装成行生成器。

B3 每 13 个 Runtime B 基准 tick 调用一次，调用率 346.153846Hz；32 样本块的音频采样率为 11076.923076923076Hz。该局部链使用独立 `b3_audio*` 类型，不再标成 48kHz。

## 算法与参数

TOP p11 提供：

- 54 项 1-based channel-to-ramper map，lowering 转为 Orpheus 0-based map；
- offset 128；
- `MutesBass=0`；
- 默认 ramp 30ms；
- 30 项 index/dB 表（有效前 15 项，尾部重复 255/-40）。

四个 ramper 对应 left/right/center/mono。每个样本按指数系数推进，每块结束 `frameCount` 减一；freeze 时 gain 与 frameCount 均不变。非零 dB/s 优先，范围 5..50000；时间范围 1..3000ms。

## 冷启动与 RTC

`Model_1_2_StructStaticInit.c` 明确四路初值：

```text
frameCount=0, rampCoeff=1, targetGain=1, currentGain=1, freeze=0
```

因此本实例配置 `apply_index_on_prepare=false`，启动时保持 unity，首次 `gain_index` 更新后才计算目标。通用组件把同一块边界收到的 index/ramp 参数暂存到 process 开头统一重算，避免 RTC 字段写入顺序改变 ramp。

## Composite

```text
peripheral10 --split--\
overhead4 -----split----> interleave54 -> sleeping_beauty54 -> split54 -> peripheral10/overhead4/headrest40
headrest40 ----split---/

headrest40 -> split40 -> row1_20 + row2_20
```

全部使用标准音频连接，无第三种边类型。

## Golden

直接 ABI 测试覆盖：

- 源等价逐样本指数 ramp；
- freeze；
- 54ch 与 `-1` 静音映射；
- unity cold-start；
- 同块 index/ramp 参数原子应用；
- Balance 复用策略不回归。

Algorithm IR 测试固定 54 项 map、30 点表、110 条 composite 连接、40 条行拆分连接与 B3 真实速率。

## 六门禁

| boundary | schedule | algorithm | state | parameters | golden |
|---|---|---|---|---|---|
| verified | verified | verified | verified | verified | verified |
