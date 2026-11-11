# gain_ramper - 声道分组增益斜坡

> Orpheus 通用原语组件（L0），对应外部模型的 Rgainx/Rgainy 块。

## 功能

多通道指数增益斜坡器。N 个独立 ramper 各自维护 `currentGain/targetGain`，在 dB 域以恒定速率指数趋近目标增益。通道->ramper 映射表决定每个通道使用哪个 ramper 的增益。

是 SleepingBeauty、FadeControl、MuteControl 三个高级组件的共同基座。

## 端口

| 端口 | 方向 | 类型 | 通道 |
|------|------|------|------|
| `in` | 输入 | audio f32 | param:channels |
| `out` | 输出 | audio f32 | param:channels |
| `gain0` | 可选输出 | control f32 | 1帧×1通道 |

接口事实源为声明文件，独立生成入口：

```powershell
python scripts\generate_component.py components\orpheus\builtin\gain_ramper\component.yaml
```

`in` 每次真实到达即处理，不按Task去重。两份输出先准备完，
再由算法依次交付 `out(1)`、`gain0(2)`；编号不是调度优先级。
观测值是 `gain_channel` 的块末已应用增益，不是目标或首样本增益。
旁路为1，模式2静音为0；1帧载荷不改变事件时间量子。
精确原地合法，部分重叠、与状态或观测出口重叠拒绝。
下游失败停止后续出口，不回滚已推进的包络。

## 参数

| 参数 | 类型 | 默认 | 范围 | 更新策略 | 说明 |
|------|------|------|------|----------|------|
| `gain_db` | float | 0.0 | [-96, 24] | smoothed | 目标增益（dB），所有 ramper 共享 |
| `ramp_ms` | float | 30.0 | [0, 5000] | restart | 斜坡时间（ms） |
| `channels` | int | 2 | [1, 64] | restart | 通道数（影响签名） |
| `num_rampers` | int | 1 | [1, 32] | restart | ramper数量及资源活动维度（影响签名） |
| `chan_map` | string | "" | - | restart | 通道->ramper映射，-1=bypass，模式2额外允许-2静音 |
| `execution_mode` | int | 0 | [0, 2] | restart | 块级dB请求／逐样本递推／帧内幂次 |
| `freeze` | bool | false | - | block_boundary | 显式模式冻结包络及剩余计数 |
| `gain_output` | int | 0 | [0, 1] | restart | 启用gain0，影响签名 |
| `gain_channel` | int | 0 | [0, channels-1] | restart | 观测声道，启用出口时检查实例范围 |

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
- 此段dB请求算法仅适用于模式0；运行期只允许该模式设置`gain_db`。
- 模式1在每个样本前乘递推系数；模式2以块内起值乘系数的`n+1`次幂，
  不替换为逐样本递乘。两种模式每次真实调用减一次剩余块数，
  计数已为零才在本次样本处理前吸附目标，冻结时不吸附、不减计数。
  模式2保留负计数并在`INT32_MIN`饱和；模式1拒绝负计数。
- 模式0的reset恢复当前dB目标；显式模式reset保留包络和资源。
  显式模式零帧不推进、不交付旧值；模式0零帧仍推进一次块级斜坡，
  只交付零帧音频、不发布旧标量。
- 模式0映射保留负数旁路、越界组回到0、缺失通道补0；
  改为完整解析，不再截断256字节文本或把非数字静默当作0。

## BULK 槽

`current_gains`、`target_gains`、`coefficients`为f32资源，
`remaining_blocks`为精确i32资源；物理容量32项，活动shape为`num_rampers`，
均双缓冲。配置初值仍为同名CSV字符串，运行期按数值数组整组更新。
显式模式空串采用增益/系数1、计数0，非空必须完整声明全部活动项；
非有限、负增益、缺项与垃圾拒绝。计数不经float中转。
数值存储在算法私有状态中，壳从同一声明生成资源绑定，不另建重复事实源。

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

| 外部模型源码 | 本组件 |
|---|---|
| `blocklib/lib/preamp/rgainx.slx` | 整体对应 |
| `rgainx_Mask.m` RamperState {currentGain, targetGain, rampCoeff, frameCount} | `RamperSlot` 结构 |
| `Model_1_1.c:5039-5230` ramper 更新逻辑 | [算法实现](user/algorithm.c)中的参数更新钩子与处理入口 |
| `rgain_SILENT_GAIN = 5.0118723e-7` | `GR_SILENT_GAIN` |
