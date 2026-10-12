# B.BANDS.MONO 蒸馏记录

> 状态：已验证并替换 placeholder
>
> 实现：`orpheus.builtin.matrix_mul`

## 边界

- 输入：10ch × 32 frames
- 输出：1ch × 32 frames
- Task：B1
- 状态：无跨块状态

## 源码证据

权威源根：`C:\D\Work\Project\EREV\cart-cicd-erev\components\baf\src\out\baremetalgxp\slx\code`

- `Model_1_2_ert_shrlib_rtw/MedusaPart1ResettableSubsystem.c`
  - `b_0 = [0, 4, 6, 7, 2, 3]`
  - `c = [1, 5, 8, 9, 2, 3]`
  - `LeftOut = LeftIn * LeftMixWeights'`
  - `RightOut = RightIn * RightMixWeights'`
  - `Mono = mean([LeftOut RightOut], 2)`
- `Model_1_2_ert_shrlib_rtw/Model_1_2_PreAmp_p1_b0_TOP.c`
  - `MedusaBassManagerLeftMixWeights = [1, 0, 0, 0, 0, 0]`
  - `MedusaBassManagerRightMixWeights = [1, 0, 0, 0, 0, 0]`

两个源文件 SHA-256 均由 `algorithm-index.yaml` 固定，并通过 `verify_source_fingerprints` 校验。

## 推导

默认左权重只选 `b_0[0] = 0`，默认右权重只选 `c[0] = 1`：

$$
y[n] = \frac{x_0[n] + x_1[n]}{2}
$$

因此可精确 lowering 为 1×10 矩阵：

```text
[0.5, 0.5, 0, 0, 0, 0, 0, 0, 0, 0]
```

不需要项目独有 C 组件。

## 验证

`test_b_bands_mono_verified_lowering_matches_default_basis_vectors` 对 10 个 one-hot 输入逐路检查：

- ch0 → 0.5
- ch1 → 0.5
- ch2..ch9 → 0

生成 manifest 为 composite wrapper，flatten 后节点是：

```yaml
component: orpheus.builtin.matrix_mul
params:
  rows: 1
  cols: 10
  matrix: 0.5, 0.5, 0, 0, 0, 0, 0, 0, 0, 0
```

## 六门禁

| 门禁 | 状态 | 依据 |
|---|---|---|
| boundary | verified | 10ch → 1ch，32-frame block |
| schedule | verified | B1 调用 |
| algorithm | verified | WeightedSum + mean 源码 |
| state | verified | 无跨块状态 |
| parameters | verified | TOP 默认左右权重与选择索引 |
| golden | verified | 10 路 one-hot basis vectors |
