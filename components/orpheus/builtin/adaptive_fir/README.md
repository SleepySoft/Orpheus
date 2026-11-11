# 分解的 FxLMS 主动降噪（可查看链路）

> 样例工程：`examples/anc_fxlms_decomposed.yaml`。用复合组件机制（subcomponent）把 FxLMS 拆成一组通用原子，在画布上双击即可展开查看整条链路。

## 为什么能拆（核心思路）

上一版的 `anc_fxlms` 是一个黑箱原子，所有逻辑封装在里面。这个版本只封装一件事：
把“误差计算 + 权重更新”这个**有状态且必须同一样本内完成**的部分，留在一个通用原子 `adaptive_fir` 里。
其余部分全部用现成通用原子在画布上摆开。

原因：Orpheus 的数据流图不允许回馈环（Cycle Validation）。FxLMS 的更新 `w <- w + mu*e*deriv` 里，
误差 `e = d - g*y` 依赖同一样本的输出 `y`，这是一个环。因此把这个环闭在一个原子内，
外层只需提供独立的输入（原始 x、filtered-x deriv、误差麦 d），就不会产生数据流环。

## 展开后的链路（所有节点可见）

```
       外部参考麦 x —————————┌─── x （原始）
          │                                ▼
          │                          [ adaptive_fir ] = 自适应核心
          │      ┌───────────────▼  │
          │  [gain_src] 平方拆分/副本      │  │
          │      │                            │  │  y = w^T x
          │  [sdelay] 次级延迟 Δ          │  ▼  │
          │      │                            │  [neg] = -y   → 扬声器
          │  [sgain] 次级徔益 g          │
          │      │ filtered-x x' ———┌—> deriv （更新用）
          │
   误差麦 d —————————┌──── err（本为独立输入）
           内部: e = d - g*y； w += mu*e*x'/(||x'||^2+eps)
```

## 每个通用原子的角色

| 原子 | 角色 | 说明 |
|---|---|---|
| `orpheus.builtin.gain` | gain_src | 0dB前后端，用作平方拆分点，同时供给原始 x 和次级延迟。 |
| `orpheus.builtin.delay_line` | sdelay | 次级路径延迏 Δ（样本数）。 |
| `orpheus.builtin.gain` | sgain | 次级路径徔益 g（dB）。与 sdelay 合起来就是次级路径模型 S(z)=g·z^{-Δ}。 |
| `orpheus.builtin.adaptive_fir` | core | FxLMS 核心：x 读出、filtered-x deriv 更新、err 目标；内部算 e=d-g*y 并更新权重。 |
| `orpheus.builtin.negate` | neg | 取反相 -y，作为扬声器的抵消信号。 |

## 参数怎么调

- `core.filter_length` / `core.step_size` / `core.secondary_gain`：与黑箱版相同，见 `anc_fxlms/README.md` 。
- `sdelay.delays_samples` 与 `sgain.gain_db`：分别对应次级路径模型的延迟与徔益（在这里可以直接抽出调整，更直观）。

## 用法（端口怎么接）
`adaptive_fir` 是 FxLMS 的核心原子，三个输入一个输出：
- `x`：外部参考麦的“原始”信号（用作读取延迟向量）。
- `deriv`：经次级路径模型滤波后的“filtered-x”信号（用作更新）
- `err`：误差麦 `d`（目标）。本原子内部算 `e = d - g*y`。
- `out`：自适应滤波器输出 `y = wᵀ x`（通常再接 `negate` 取反相后接扬声器）。

在分解版工程 `examples/anc_fxlms_decomposed.yaml` 中，`core` 节点的接线为：
```yaml
connections:
  - {from: gain_src:out,   to: core:x}      # 原始 x
  - {from: sgain:out,      to: core:deriv}  # filtered-x x′ = S(z)·x
  - {from: d_in:out,       to: core:err}    # 误差麦 d
  - {from: core:out,       to: neg:in}      # y → 取反 -y
```
readback：`conv_metric`（收敛指标，趋零表示正常收敛）与 `detail` JSON。

## 链式接口契约

- `x` 是唯一驱动入口；`deriv` 与 `err` 仅提供数据，不触发、不等待会合。
  上游必须先完成两者的数据准备，再向 `x` 实际交付。三个输入均须绑定且帧数足够。
- 每次正帧调用保留逐通道、逐样本、逐tap的f32次序；输出用原始参考历史，
  归一化与更新用filtered-x历史，误差仍为 **`d-secondary_gain*y`**，
  不同于直接接收外部误差的 `nlms`。
- 提交权重、两组历史、位置和本次RMS指标后显式 `emit out(1)`；
  下游失败不回滚，同拍多次交付分别执行。零帧不推进、不交付。
  输入/输出别名及私有状态别名拒绝；reset清历史、权重与指标，保留配置。
- process只计算数值，不格式化JSON。`detail` 在非实时getter中由最新指标生成，
  名称、ID与生成代码的探针枚举均走同一getter；非有限指标明确报错，不输出无效JSON。
  prepare/reset后读取的指标为0，不保留旧JSON。

## 验证

- `orpheus_core/tests/test_adaptive_fir_interfaces.py`：4图×动态/生成×全局/Task，
  独立非环形f32参考逐位比较输出与RMS，并验证数据入口不驱动、同拍重复交付和JSON读回。
- `tests/adaptive_fir_smoke.c`：四次分配失败清理/重试、下游错误后继续、实时路径不格式化、
  缺失/短输入、容量、别名、零帧、reset、非法配置事务及非有限JSON错误。
- 原分解工程及其 `test_anc_decomposed.py` 仍待普通工程批次按真实来源顺序整体迁移，
  不能用旧静态调度的接线顺序作为新链式契约。
