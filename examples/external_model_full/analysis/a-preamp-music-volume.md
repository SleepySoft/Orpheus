# A.PREAMP.MUSIC_VOLUME 蒸馏记录

> 状态：已验证并替换 placeholder
>
> 实现：`delay_line(72 samples) → indexed_gain`

## 源码事实

- 10ch × 32 frames，Task A0；
- Volume 前有 10 路 72-sample 对齐延迟；
- 表：index `[0,111,176,200,255,255,255,255]`，dB `[-128,-72,-39.5,-27.5,0,0,0,0]`；
- dB 域线性插值，index 上限 255；offset 钳到 `[-10,0]`，max gain=0dB；
- 一个共享 ramper 逐样本指数推进，100ms 默认时长；非零 dB/s 优先；freeze 保持 gain/frameCount；
- `Model_1_1_StructStaticInit.c` 明确 cold-start `frameCount=0,rampCoeff=1,target=current=.04`。

## 通用组件

`orpheus.builtin.indexed_gain` 支持：

- 自定义 index/dB 表；
- gain offset 与 max gain；
- 逐样本 ramp、dB/s、freeze；
- `initial_gain` 与 `apply_index_on_prepare` 分离，允许准确表达“冷启动状态不等于缓存 RTC index”；
- `current_gain` 标准 readback/control source。

Music Volume 配置 `initial_gain=.04`、`apply_index_on_prepare=false`。首次 RTC `gain_index` 更新后才按表启动 ramp。

## Composite

```text
10ch input
  -> delay_line(channels=10, delays=[72×10])
  -> indexed_gain(table/offset/ramp/cold-start)
  -> 10ch output
```

公开参数：`gain_index`、`gain_offset_db`、`ramp_db_per_s`、`ramp_ms`、`freeze`；公开控制源：`current_gain`。

## Golden

真 Runtime lookup：

- index111 → -72dB
- index176 → -39.5dB
- index200 + offset -10 → -37.5dB
- index255 → 0dB

`delay_line` 现有测试验证逐通道样本延迟；Algorithm IR 测试固定 10 路均为 72 samples 和 `.04` 冷启动。

## 六门禁

| boundary | schedule | algorithm | state | parameters | golden |
|---|---|---|---|---|---|
| verified | verified | verified | verified | verified | verified |
