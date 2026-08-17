# sleeping_beauty - 响度补偿

> Orpheus 高级组合组件（L2），对应 Symphony SAS 的 FullRateSleepingBeauty 子系统。

## 功能

Symphony SleepingBeauty 响度补偿算法。根据音量位置（gain_index）应用非对称 L/R 增益锥度，经 4 路指数斜坡器平滑输出。核心是"低音量提升、高音量衰减"的响度曲线 + 平衡位置的非对称衰减。

## 端口

| 端口 | 方向 | 类型 | 通道 |
|------|------|------|------|
| `in` | 输入 | audio f32 | param:channels |
| `out` | 输出 | audio f32 | param:channels |

## 参数

| 参数 | 类型 | 默认 | 范围 | 更新策略 | 说明 |
|------|------|------|------|----------|------|
| `gain_index` | float | 128.0 | [0, 255] | smoothed | 增益位置（0=最小, 128=中心, 255=最大） |
| `offset` | float | 128.0 | - | restart | 中心位置 |
| `mutes_bass` | float | 0.0 | [0, 1] | restart | 极端位置时是否静音低音 |
| `ramp_ms` | float | 30.0 | [0, 5000] | restart | 斜坡时间（ms） |
| `channels` | int | 4 | [1, 32] | restart | 通道数（影响签名） |
| `chan_map` | string | "0,1,2,3" | - | restart | 通道->ramper 映射（0=left,1=right,2=center,3=mono,-1=bypass） |
| `table_idx` | string | "0,10,31,...,255" | - | restart | 锥度增益索引表（15 点默认） |
| `table_db` | string | "-40,-30,...,-40" | - | restart | 锥度增益 dB 表（15 点默认） |

## 三层结构

```
gain_index ──> [1. TaperGainLUT] ──> cut_linear
                  │
                  v
             [2. BalanceTaper] ──> targetGains[4] = {left, right, center, mono}
                  │                   delta > 0: left=center=mono=cut, right=1
                  │                   delta < 0: right=center=mono=cut, left=1
                  │                   极端: 衰减侧=0, 可选 mute bass
                  v
             [3. 4x Ramper] ──> currentGain[4] (指数斜坡)
                  │
                  v
             [4. ChanMap] ──> 每通道 × currentGain[map[ch]]
```

### TaperGainLUT

30 点查表（默认 15 点），给定 gainIdx 查找对应 dB 增益：
- 首段：线性插值到零 `cut = (gainIdx/TableIdx[0]) * 10^(TableDb[0]/20)`
- 其余段：dB 域线性插值 `cut = 10^(interpolated_db / 20)`

默认表（来自 `SleepingBeautyConfig.m`）：
```
TableIdx = [0, 10, 31, 52, 74, 95, 116, 128, 138, 159, 180, 202, 223, 244, 255]
TableDb  = [-40, -30, -20, -10, 0, 0, 0, 0, 0, 0, 0, -10, -20, -30, -40]
```

### BalanceTaper

```c
delta = gainIdx - offset;
if (delta > 0) { left=center=mono=cut; right=1; }      // 左侧衰减
else            { right=center=mono=cut; left=1; }       // 右侧衰减
if (|delta| >= offset-1) { 衰减侧=0; }                   // 极端=完全静音
if (极端 && mutesBass) { mono=0; }                       // 可选静音低音
```

### 4x Ramper

4 路独立指数斜坡器（left/right/center/mono），复用 gain_ramper 的 `rampCoeff = ln(target/current) / numBlocks` 逻辑。每块更新一次 currentGain。

## 使用示例

```yaml
- id: sb
  component: orpheus.builtin.sleeping_beauty
  params:
    gain_index: 128.0
    offset: 128.0
    ramp_ms: 50.0
    channels: 22
    chan_map: "0,1,0,1,2,3,-1,-1,0,1,0,1,2,3,-1,-1,0,1,0,1,2,3"
    table_idx: "0,10,31,52,74,95,116,128,138,159,180,202,223,244,255"
    table_db: "-40,-30,-20,-10,0,0,0,0,0,0,0,-10,-20,-30,-40"
```

## 源码映射

| Symphony SAS 源码 | 本组件 |
|---|---|
| `Model_1_1.c:13515` calculate_SB_gains | `sb_calculate_gains()` |
| `Model_1_1.c:13680` calculate_ramp_parameters | `sb_ramper_set_target()` |
| `Model_1_1.c:13725` control (ramper 更新) | `sb_process()` ramper 更新段 |
| `SleepingBeautyConfig.m` DefaultTaperGainTable | `table_idx`/`table_db` 默认值 |
| `SleepingBeautyConfig.m` NumRampers=4 | `SB_MAX_RAMPERS=4` |
| `SleepingBeautyConfig.m` ChanToRampMap | `chan_map` 参数 |
| 参数分区 p12_b0 | 全部参数覆盖 |
| RTC: PreAmpSymphonySleepingBeautyFrSet | `gain_index` smoothed 参数 |
