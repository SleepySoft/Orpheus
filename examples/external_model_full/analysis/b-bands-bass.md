# B.BANDS.BASS 蒸馏记录

> 状态：已验证并替换 placeholder
>
> 实现：`matrix_mul(10→11) → iir_bank(11ch×3 stages) → channel_router ×3`

## 边界修正

旧 IR 把结果压成不存在的 `audio4`。源码 `MedusaPart1ResettableSubsystemTID3` 实际产生三条独立标准音频流：

- `hpf_lr`: 2ch，送 B FDP；
- `hpf_other`: 7ch，送 Deci Mix；
- `bass_lpf`: 2ch，送 Deci Mix。

输入为 3/13 SRC 后的 10ch × 32 frames。B3 每13个 Runtime B 基准 tick 调用一次，音频采样率 11076.923076923076Hz。

## 输入构造

11×10 矩阵一次构造：

```text
HPF[0] = input[2] + input[3]       # C + GainLfe2C*LFE，GainLfe2C=1
HPF[1:9] = input[0], input[1], input[4:10]
LPF[0] = input[0]                  # TOP left weights [1,0,0,0,0,0]
LPF[1] = input[1]                  # TOP right weights [1,0,0,0,0,0]
```

LPF 加权和直接取原始 SRC 输入，不取 HPF 输出。

## PoolIIR 转换

九路 HPF 与两路 LPF 均为 3 stages。PoolIIR 的首级 `[gain,a2,a1,b2,b1]`、后级 `[a2,a1,b2,b1]` 转为通用 DF2T `[b0,b1,b2,a1,a2]`；首级 gain 已吸收到 numerator。

- HPF stage0: `[.920707226,-.920707226,0,-.95020926,0]`
- HPF stage1: `[1,-2,1,-1.9182049,.920707285]`
- HPF stage2: `[1,-2,1,-1.96638691,.968952119]`
- LPF stage0: `[9.987561e-9,9.987561e-9,0,-.95020926,0]`
- LPF stage1/2 与 HPF 相同 denominator，numerator 为 `[1,2,1]`

使用 `iir_bank coefs_mode=per_channel`，共 165 个系数。冷启动状态全零；本路径无 bypass/enable 分支。

## 输出顺序

过滤后的11路拆为：

```text
hpf_lr    = filtered[1,2]
hpf_other = filtered[0,3,4,5,6,7,8]
bass_lpf  = filtered[9,10]
```

B Runtime 调用链已同步改为三条标准音频连接，不再保留 `bass_mid: audio4`。

## Golden

- Algorithm IR 测试固定 11×10 非零矩阵位置、三流端口和 165 系数；
- 真 Runtime 双通道 impulse 同时运行源 HPF/LPF 三阶段系数；
- Python 逐样本 DF2T 参考递推与 WAV 输出比较，覆盖完整 128 samples。

## 六门禁

| boundary | schedule | algorithm | state | parameters | golden |
|---|---|---|---|---|---|
| verified | verified | verified | verified | verified | verified |
