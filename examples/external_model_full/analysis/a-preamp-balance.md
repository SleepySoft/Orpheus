# A.PREAMP.BALANCE 蒸馏记录

> 状态：已验证并替换 placeholder
>
> 实现：通用 `orpheus.builtin.sleeping_beauty`，`center_mono_mode=mean_lr`

## 边界与状态

- 输入/输出：10ch × 32 frames
- Task：A0，48 kHz
- 四组共享 ramper：left/right/center/mono
- 通道映射（外部 1-based → Orpheus 0-based）：`0,1,2,3,0,1,0,0,1,1`
- 状态：每组 current/target/per-sample coefficient/frameCount；支持 freeze

## 源码证据

- `Model_1_1.c`：Balance target 计算、逐样本 ramp、frameCount、freeze 和 `-1` 静音；
- `Model_1_1_PreAmp_p0_b0_TOP.c`：30 点 index/dB 表、offset=128、ramp=30ms、10ch map、silent extreme 与 bass mute；
- `evidence/big6-defaults.json`：固定 TOP 字段、shape 和 hash；
- 29 个 Algorithm IR source fingerprints 已与外部源码逐字节校验。

冷启动四组 current/target 为 unity、frameCount=0。Orpheus 显式使用 neutral `gain_index=128` 保持相同启动输出，并公开 RTC 参数。

## 与 SleepingBeauty 的关系

两者共享 LUT、四 ramper、逐样本指数推进、freeze 和极值策略。区别是中心/低音目标：

- SleepingBeauty：跟随衰减侧；
- Balance：`center = mono = (left + right) / 2`。

因此通用组件新增 `center_mono_mode=attenuated|mean_lr`，没有新增项目专用 C 组件。

## 固定参数

```yaml
channels: 10
offset: 128
mutes_bass: 1
ramp_ms: 30
center_mono_mode: mean_lr
chan_map: 0,1,2,3,0,1,0,0,1,1
table_idx: 0,10,31,52,74,95,116,128,138,159,180,202,223,244,255,...
table_db: -30,-27,-14,-7,-5,-3,-1,0,-1,-3,-5,-7,-14,-27,-30,...
```

公开参数：`gain_index`、`ramp_db_per_s`、`ramp_ms`、`freeze`。

## Golden

- index128：10ch 全 unity；
- index74：right=`10^(-5/20)`，center/mono=`(1+right)/2`；
- index0/255：衰减侧与 center 静音到 -126dB floor，另一侧 unity；
- 128→74：45 blocks，首块逐样本分别按 right 与 mean target 的指数系数推进；
- freeze：currentGain 和 frameCount 均不变化。

直接 DLL ABI 测试见 `test_sleeping_beauty.py`。

## 六门禁

| boundary | schedule | algorithm | state | parameters | golden |
|---|---|---|---|---|---|
| verified | verified | verified | verified | verified | verified |
