# gain_ramper - 多通道增益斜坡器

> Orpheus 通用原语组件（L0），对应 BAF SAS 的 Rgainx/Rgainy blocklib 块。

## 功能

多通道指数增益斜坡器。N 个独立 ramper 各自维护 `currentGain/targetGain`，在 dB 域以恒定速率指数趋近目标增益。通道->ramper 映射表决定每个通道使用哪个 ramper 的增益。

是 SleepingBeauty、FadeControl、MuteControl 三个高级组件的共同基座。

## 端口

| 端口 | 方向 | 类型 | 通道 |
|------|------|------|------|
| `in` | 输入 | audio f32 | param:channels |
| `out` | 输出 | audio f32 | param:channels |

## 参数

| 参数 | 类型 | 默认 | 范围 | 更新策略 | 说明 |
|------|------|------|------|----------|------|
| `gain_db` | float | 0.0 | [-96, 24] | smoothed | 目标增益（dB），所有 ramper 共享 |
| `ramp_ms` | float | 30.0 | [0, 5000] | restart | 斜坡时间（ms） |
| `channels` | int | 2 | [1, 32] | restart | 通道数（影响签名） |
| `num_rampers` | int | 1 | [1, 8] | restart | ramper 数量 |
| `chan_map` | string | "0,0" | - | restart | 通道->ramper 映射，-1=bypass |

## 算法

```
当 gain_db 变化:
  targetGain = 10^(gain_db/20)
  curDb = 20*log10(max(current, SILENT))
  tgtDb = 20*log10(max(target, SILENT))
  diff = |tgtDb - curDb|
  numBlocks = ramp_ms/1000 * sampleRate / blockSize
  rampCoeff = ln(target/current) / numBlocks

每块处理:
  currentGain *= exp(rampCoeff)   // 指数趋近
  if 达到或越过 target: currentGain = target, rampCoeff = 0

每样本:
  out[ch] = in[ch] * rampers[chanMap[ch]].currentGain
```

- 静音下限：`SILENT_GAIN = 5.0118723e-7f`（-126 dB，与源码 `rgain_SILENT_GAIN` 一致）
- 每块更新一次 ramper（per-frame，非 per-sample，与源码一致）

## BULK 槽

无 BULK 槽。所有参数走标量槽。

## 使用示例

```yaml
- id: ramper
  component: orpheus.builtin.gain_ramper
  params:
    gain_db: -6.0
    ramp_ms: 100.0
    channels: 4
    num_rampers: 2
    chan_map: "0,1,0,1"   # ch0,2 -> ramper0; ch1,3 -> ramper1
```

## 源码映射

| BAF SAS 源码 | 本组件 |
|---|---|
| `blocklib/lib/preamp/rgainx.slx` | 整体对应 |
| `rgainx_Mask.m` RamperState {currentGain, targetGain, rampCoeff, frameCount} | `RamperSlot` 结构 |
| `Model_1_1.c:5039-5230` ramper 更新逻辑 | `ramper_set_target()` + process |
| `rgain_SILENT_GAIN = 5.0118723e-7` | `GR_SILENT_GAIN` |
