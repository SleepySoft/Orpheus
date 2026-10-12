# A.PREAMP Bass / Treble / Mid 蒸馏记录

> 状态：三个叶节点均已验证并替换 placeholder
>
> 实现：通用 `orpheus.builtin.indexed_tone`

## 共同行为

- 10ch × 32 frames，Task A0；
- $u=clamp((index-128)/127,-1,1)$，boost/cut 最大值均为 20dB；
- $G=10^{impact/20}$；
- 输出统一为 $y=x+(G-1)f(x)$；
- 每通道独立滤波状态，所有通道共享 gain ramper；
- 30ms 默认逐样本指数 ramp，48k/32 对应 45 blocks；
- freeze 只冻结 gain/frameCount，滤波状态继续推进；
- 跨 unity 时分两阶段，使用 `1±4.7683716e-7` 避免相位歧义；
- 冷启动 state：`frameCount=0,current=target=1,coeff=1,phase=1`，因此 lowering 显式使用 index128。

## Raw 系数

统一顺序 `[b0,b1,b2,a1,a2]`，boost/cut 当前 TOP 相同。

| 模块 | order | 系数 |
|---|---:|---|
| Bass | 1 | `0.00779292, 0.00779292, 0, -0.98441416, 0` |
| Treble | 1 | `0.788675129, -0.788675129, 0, -0.577350259, 0` |
| Mid | 2 | `0.0532660671, 0, -0.0532660671, -1.88373291, 0.893467903` |

组件内部使用标准 DF2T 计算 $f(x)$，再做 dry+scaled-filter 混合。

## 源码证据

- `Model_1_1.c`：`tone` / `tone_l` / `tone_o`、index-to-linear、rgainy ramp/control；
- `Model_1_1_PreAmp_p0_b0_TOP.c`：三组 boost/cut 系数、offset/scale、±20dB、30ms；
- `Model_1_1_StructStaticInit.c`：三组 ramper cold-start 与 phase/freeze；
- `evidence/big6-defaults.json`：固定默认参数、shape 与 SHA-256；
- 所有 Algorithm IR source fingerprints 已逐字节验证。

## Golden

真 Runtime impulse 测试对三个系数集分别计算同一 DF2T 参考，并比较前 16 个输出样本。

index255（$G=10$）时 reset-state 单位脉冲首样本：

- Bass：$1+9×0.00779292=1.07013628$
- Treble：$1+9×0.788675129=8.09807616$
- Mid：$1+9×0.0532660671=1.47939460$

index128 时 $G=1$，输出严格 dry，但滤波状态仍更新。跨 unity、freeze 和 frame ramp 由同一通用状态机实现。

## 公开参数

三个 composite 都公开 `gain_index`、`ramp_db_per_s`、`ramp_ms`、`freeze`，映射到通用组件标准参数。

## 六门禁

| boundary | schedule | algorithm | state | parameters | golden |
|---|---|---|---|---|---|
| verified | verified | verified | verified | verified | verified |
