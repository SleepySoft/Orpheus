# A Route Level / PostProcess 未蒸馏项审计

> 更新：2026-10-13
>
> 目的：解释三个节点为何仍为 placeholder，并固定下一步实现边界。它们不是组件 catalog 缺失。

## A.PREAMP.ROUTE_LEVEL

### 已确认边界

- A0，48kHz，32 frames/block；
- `program10`：InputMixer 输出，供 detector 支路；
- `volume10`：Volume 输出，按32项运行期 map 形成 `post_buffer32`；
- `balanced10`：直接成为 Audiopilot program；
- `microphone1`：直接成为 Audiopilot microphone；
- detector 输出两路 block-rate level（Audiopilot/DynEQ），不是音频。

默认32项 map 为 `[1..6]` 重复5次再接 `[1,2]`；RTC 非零项覆盖 TOP，0 表示保留 TOP，最终值不在1..6时对应输出静音。

### Detector

```text
program10 -> 72-sample delay -> 10ch one-stage PoolIIR pre-emphasis
          -> frame RMS/peak dB (当前 mode 固定选 RMS)
          -> AP InnerLink + DynEQ InnerLink
```

两套 InnerLink 状态独立。默认 hold=4500次 block 调用（约3秒），fast decay=-0.00333333341/block，slow decay=-6.66666674e-5/block，hold margin=6dB，范围[-80,0]dB。最终 level 还使用 Volume ramper 的当前 gain dB，不能由音频可靠反推。

### 剩余实现

- 通用 block-boundary 整数路由表（0 fallback + 非法静音）；
- frame RMS/peak dB + hold/decay tracker；
- Volume 当前 gain dB 控制源和两路 level 消费者；
- `PreAmpReset1` 对 delay/IIR/InnerLink 的复位证据与 goldens。

## A.POSTPROCESS.PROTECTED

### 已确认顺序

```text
Part6 protected22
 -> 22 independent samplewise input rampers
 -> source-recursive limiter (unless LimiterSoftclipDisable)
 -> PostEQ PoolIIR (shutdown => zero)
 -> internal piecewise soft clip (same disable)
 -> shared samplewise RMDL mute
 -> normal output
```

Test mode 是独立的22×10矩阵分支，直接取 input-ramped audio，绕过 limiter/PostEQ/internal clip/RMDL mute。

Limiter 不是不可见 vendor 黑盒；生成源码给出递推：

$$
g \leftarrow \min(1,\max((1+a)-\max(k_1g|u|,a),m)\,g\,d),\qquad y=ug
$$

high/low 默认均为 `attack=.0236999, decay=1.00023985, k1=.0118499501, maxAttack=.316227764`。现有通用 limiter 的 envelope/threshold 公式不同，不能替代。

内部软削波可复用 `piecewise_soft_clipper`，默认 `.65/1.35/.714285731`。真正未决项是 samplewise ramp-bank、递归 limiter、PostEQ 560-float PoolIIR 非平凡布局及完整 reset/golden。

## A.POSTPROCESS.ROUTER_OUTPUT

### 源码证明的71路边界

`Model_1_1.c::Model_1_1_PostProcess_c` 明确拼接顺序：

```text
candidate[0..21]   = protected22 (PostProcess.c 写入 offset 0)
candidate[22..38]  = announcement17 (InputSelect 写入 offset 704)
candidate[39..70]  = preq32 (写入 offset 1248)
```

### 已确认顺序

1. Main：22项 1-based select + per-output gain；select<=0输出零，>71安全夹到71；RTC 最高位为1时回退TOP。
2. AUX：当 enable 非零，22项 AuxSelect 选择17路 announcement 并乘 AuxGain，加到 main 输出；无效项不加。
3. 共享 samplewise AllMute。
4. 第二个 `piecewise_soft_clipper`，默认 `xmin=2,xmax=4,p2=.25`。
5. OutputCal：22路运行期 gain，逐项 clamp 到[0,1]，cold-start全1。
6. FreqComp PoolIIR；shutdown时输出零。
7. 可开关22路 tunable pool delay；TOP 默认全零。

### 剩余实现

- 通用 71→22 main+aux 动态数组路由；
- samplewise AllMute；
- FreqComp 175-float PoolIIR 布局与 accelerator goldens；
- RTC fallback、非法索引、AUX叠加、OutputCal、shutdown、delay 的逐块测试。

## UI 状态含义

- `indexed_gain` / `indexed_tone`：已实现 builtin；若显示“组件缺失”是服务端 catalog 缓存问题。
- 上述三个 `external_model.index.*`：仍显示“未蒸馏”是正确状态，因为至少一个六门禁未通过。不得用已有近似组件强行去掉 placeholder 徽标。
