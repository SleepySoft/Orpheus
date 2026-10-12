# A.SPATIAL_FULL.POST_HOLOGRAM 蒸馏记录

> 状态：已验证并替换 placeholder
>
> 实现：`sleeping_beauty(22ch) → delay_line(22ch)`

## 边界与调度

- 输入/输出均为 22ch × 32 frames，通道顺序由前级 Peripheral EQ 确定，本模块不重排；
- Task A0，每 32 样本调用一次，48kHz；
- 调用顺序：Peripheral EQ → Post Hologram → Fade/Sum。

## SleepingBeauty

TOP p12 的 1-based map 转为 Orpheus 0-based：

```text
2,2,0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1,0,0,1,1
```

参数：offset=128、MutesBass=0、默认 ramp=30ms；30 项 index/dB 表有效前15项，尾部重复 255/-40。无通道使用 mono ramper，也没有 `-1` 静音通道。

`Model_1_1_StructStaticInit.c` 明确四路 cold-start 为 `frameCount=0,current=target=coeff=1,freeze=0`；启动 index 仅存在于 RTC request，不是 TOP 初值。因此实例使用 `apply_index_on_prepare=false`，启动保持 unity。

Room-Mode reset 只重置后级 delay index，不重置 ramper。通用组件新增 `preserve_state_on_reset=true`，直接 ABI 测试证明 reset 前后的 in-flight ramp 与不中断参考逐样本一致。

## Phase Delay

TOP 的 22 项 phase delay 全为 0。源码环每通道 stride=80，块长32，因此隐含 max delay=48；写指针前进后按 `delay+32` 回读，delay=0 读取刚写入当前块，不产生额外一块延迟。

Lowering 配置：

```text
channels=22
max_delay_samples=48
delays_samples=[0 × 22]
```

## 控制

公开标准参数：`gain_index`、`ramp_db_per_s`、`ramp_ms`、`freeze`。同块更新在 process 开头一次性建立四路目标。

相邻的 FullRateHoligramDisable 和 PostHoligramRoutingDisable 分别属于前级 EQ 与后级 router，关闭时输出零，不是本叶子的 bypass。

## Golden

- 22ch map、30 项表、unity cold-start 与 reset 保持由 Algorithm IR/ABI 测试固定；
- `delay_line` 逐通道测试覆盖 delay=0/非零路径；
- SleepingBeauty 测试覆盖逐样本 ramp、freeze、同块原子更新和极值策略。

## 六门禁

| boundary | schedule | algorithm | state | parameters | golden |
|---|---|---|---|---|---|
| verified | verified | verified | verified | verified | verified |
