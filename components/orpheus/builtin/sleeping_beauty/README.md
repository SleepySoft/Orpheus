# sleeping_beauty - 声道分组平衡渐变

> 通用声道分组平衡构件；历史ID保留`sleeping_beauty`，实现来源为FullRateSleepingBeauty。

## 功能

根据索引`gain_index`查表计算非对称左右平衡目标，经左右、中央、单声道四组指数斜坡作用于映射通道。
查找表、通道映射和中央/单声道策略均可配置。不分析输入的响度，也没有低频或高频响度补偿滤波。
历史名称不能被理解为自动响度均衡或“低音量自动提升”算法。

## 端口

| 端口 | 方向 | 类型 | 通道 |
|------|------|------|------|
| `in` | 输入 | audio f32 | param:channels |
| `out` | 输出 | audio f32 | param:channels |

## 接口交付

落盘接口配置由独立 `scripts/generate_component.py` 校验并生成类型化壳，算法在 `user/algorithm.c`。
`in` 为唯一驱动入口，每次真实到达逐样本推进四组共享斜坡，处理全部映射通道后显式交付 `out(1)`。
同组通道共享同一个样本增益，不因重复映射多推进；同拍调用分别处理，下游错误不回滚已完成状态。
斜坡次数仍按prepare块长换算，每次正帧调用末递减一次，实际样本循环使用本次真实帧数；
零帧不消费次数、不建立待处理目标、不交付。
允许完整原地处理，拒绝部分重叠、私有状态别名、缺失及短输入。

## 参数

| 参数 | 类型 | 默认 | 范围 | 更新策略 | 说明 |
|------|------|------|------|----------|------|
| `gain_index` | float | 128.0 | [0, 255] | smoothed | 增益位置（0=最小, 128=中心, 255=最大） |
| `offset` | float | 128.0 | - | restart | 中心位置 |
| `mutes_bass` | float | 0.0 | [0, 1] | restart | 极端位置时是否静音低音 |
| `ramp_ms` | float | 30.0 | [0, 5000] | block_boundary | 斜坡时间（ms） |
| `ramp_db_per_s` | float | 0.0 | [0, 50000] | block_boundary | 非零时优先按 dB/s 推导斜坡时长，内部钳到 5~50000 dB/s |
| `freeze` | bool | false | - | block_boundary | 冻结 current gain 与剩余 frameCount |
| `center_mono_mode` | string | attenuated | attenuated/mean_lr | restart | 中置/低音跟随衰减侧，或取左右目标均值（Balance） |
| `apply_index_on_prepare` | bool | true | - | restart | false 时冷启动保持四路 unity，首次 `gain_index` 更新后才建立目标 |
| `preserve_state_on_reset` | bool | false | - | restart | true 时 reset 保留 ramper 状态，用于只重置相邻延迟/路由的外部模型生命周期 |
| `channels` | int | 4 | [1, 64] | restart | 通道数（影响签名） |
| `chan_map` | string | "0,1,2,3" | - | restart | 通道->ramper 映射（0=left,1=right,2=center,3=mono,-1=静音） |
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

4 路共享指数斜坡器（left/right/center/mono）按源模型逐样本推进。设块长为 $B$、斜坡块数为 $N$：

$$
k=\exp\left(\frac{\ln(target/current)}{B N}\right),\qquad g[n]=g_0 k^{n+1}
$$

每处理一块 `frameCount` 减一，归零时吸附目标；`freeze=true` 时 gain 和 frameCount 均不推进。非零 `ramp_db_per_s` 优先于 `ramp_ms`。同一块边界内收到的 `gain_index`、`ramp_ms`、`ramp_db_per_s` 会在下一次 process 开头统一计算，避免参数写入顺序改变目标斜坡。
`ramp_ms=0`始终瞬切；其余情形才按非零dB/s推导时长。
保留同值SET重新建立请求的语义：冷启动保持unity后，再次写入相同索引也能开始渐变。
待处理请求在实际输入到达前保持；按名称、ID和调用前控制链写入都走同一参数钩子。
直接改变有效控制值也会在下次真实调用检测到，不依赖只有setter才会更新的dirty标志。

CSV元素必须有限；两张表须等长、非空且不超过30项，索引严格递增。
映射只接受-1或0～3；缺少通道的部分静音，超过实际通道数的合法尾项忽略。
非法配置明确拒绝，不再截断文本或静默接受坏数值；参数验证失败不破坏旧状态。

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

| 外部模型源码 | 本组件 |
|---|---|
| `Model_1_1.c:13515` calculate_SB_gains | `calculate_gains()` |
| `Model_1_1.c:13680` calculate_ramp_parameters | `set_target()` |
| `Model_1_1.c:13725` control（逐样本 ramper + frameCount/freeze） | `orpheus_builtin_sleeping_beauty_algorithm()` |
| `SleepingBeautyConfig.m` DefaultTaperGainTable | `table_idx`/`table_db` 默认值 |
| `SleepingBeautyConfig.m` NumRampers=4 | 固定四组共享ramper |
| `SleepingBeautyConfig.m` ChanToRampMap | `chan_map` 参数 |
| 参数分区 p12_b0 | 全部参数覆盖 |
| RTC: PreAmpSleepingBeautyFrSet | `gain_index` smoothed 参数 |
