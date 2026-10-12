# A.PREAMP.INPUT_MIXER 蒸馏记录

> 状态：已验证并替换 placeholder
>
> 实现：通用 `gain` + 两个 `input_mixer_3d` + `n_way_mux`

## 边界

- 输入：12ch × 32 frames，顺序 `L,R,C,LFE,Lss,Rss,Lsr,Rsr,Ltf,Ltb,Rtf,Rtb`
- 输出：10ch × 32 frames
- Task：A0
- 状态：仅 mux 选择状态；`ramp_ms=0` 时与源 Switch 块同块切换
- 公开参数：`stereo_downmix`，0=5.1.4，1=stereo

## 源码证据

权威源根：`C:\D\Work\Project\EREV\cart-cicd-erev\components\baf\src\out\baremetalgxp\slx\code`

- `Model_1_1_ert_shrlib_rtw/Model_1_1.c`
  - `AddWeights`：LFE、Lsr→Lss、Rsr→Rss
  - `DownmixToStereo`：L/R、侧环绕、顶置、C 和加权 LFE 的左右下混
  - `Send514Out`：输出顺序 `L,R,C,LFE,Lss,Rss,Ltf,Ltb,Rtf,Rtb`
  - `Switch1`：`InputMixer3dStereoDnMix != 0` 选择 stereo，否则选择 5.1.4
- `Model_1_1_ert_shrlib_rtw/Model_1_1_PreAmp_p0_b0_TOP.c`
  - `MakeupGainMakeupGain = 1`
  - `InputMixer3dWeights_514 = [3.16, 1, 1]`
  - `InputMixer3dWeights_L_R = [1,1,1,1,1,1,0.5,0.5]`
  - `InputMixer3dStereoDnMix = false`
- `evidence/input-routing-defaults.json` 固定上述值、shape 和 hash。

29 个索引源文件已通过 `verify_source_fingerprints`。

## 通用组件组合

```text
12ch input
  -> gain(0 dB, ingress)
      ├-> input_mixer_3d(12→10, 5.1.4 matrix) -> mux.in0
      └-> input_mixer_3d(12→10, stereo matrix) -> mux.in1
  -> n_way_mux(select_index=stereo_downmix, ramp_ms=0)
  -> 10ch output
```

入口 gain 只用于把一个 composite 输入合法扇出到两张矩阵，不改变数值。

`n_way_mux` 新增通用兼容参数 `select_index`：默认 -1 时保持旧 1-based `select`；显式 0/1 时直接选择 in0/in1。该能力已通过真实 WAV 运行时测试。

## 矩阵

5.1.4 矩阵的非零项：

```text
out0=L
out1=R
out2=C
out3=3.16*LFE
out4=Lss+Lsr
out5=Rss+Rsr
out6=Ltf
out7=Ltb
out8=Rtf
out9=Rtb
```

Stereo 矩阵的非零项：

```text
out0=L + Lss + Lsr + Ltf + Ltb + 0.5*C + 1.58*LFE
out1=R + Rss + Rsr + Rtf + Rtb + 0.5*C + 1.58*LFE
out2..out9=0
```

其中 `1.58 = 3.16 × 0.5`。

## 验证

- `test_a_preamp_input_mixer_verified_lowering_matches_both_basis_matrices`：分别检查两张 10×12 矩阵的 12 路 one-hot 响应；
- `test_mux_zero_based_select_index_overrides_legacy_select`：真 Runtime/WAV 验证 0→in0、1→in1；
- 生成 manifest 为四节点 composite，flatten 后仅包含通用组件。

## 六门禁

| 门禁 | 状态 | 依据 |
|---|---|---|
| boundary | verified | 12ch → 10ch 与明确顺序 |
| schedule | verified | A0 同块处理 |
| algorithm | verified | AddWeights/DownmixToStereo/Send514Out/Switch1 |
| state | verified | mux 选择；无滤波/延迟状态 |
| parameters | verified | TOP makeup、两组权重、默认模式 |
| golden | verified | 两模式 12 路 one-hot + mux runtime |
