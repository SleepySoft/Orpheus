# orpheus.builtin.piecewise_soft_clipper - 分段二次软削波

## 功能

逐样本实现可参数化的二次分段软削波曲线：

$$
x_1=\min(|u|,x_{max}),\quad x_2=\max(x_1-x_{min},0),\quad
y=\operatorname{sign}(u)(x_1-p_2x_2^2)
$$

它与通用 `soft_clipper` 的 tanh 曲线不同，因此作为模型专用组件独立存在。

## 参数

- `xmin/xmax/p2`：高档参数。EREV-1 Model 1.1 默认 `0.65 / 1.35 / 0.714285731`。
- `xmin_low/xmax_low/p2_low`：低档参数，当前参考模型默认与高档相同。
- `param_set`：1 选择高档，0 选择低档。外部多值消息需先按 `原值==1` 归一化。
- `disabled`：直通，不执行软削波。
- `active_mask`：本块中峰值超过 `xmin` 的通道位图，只读探针。
  峰值从通道首样本开始按顺序比较；首样本为 NaN 时该通道不置位，后续 NaN 不更新峰值。
  reset 清除位图并保留曲线、档位与旁路配置。

## 接口化执行

配置经独立 CLI 生成类型化壳，算法在每次 `in` 到达后直接交付 1 号 `out`；
同 Task 多次到达分别处理，不等待、不去重。二次曲线保持原始逐项 f32 运算次序，
32 通道位图包含第 31 位，按 int32 原始位型回读。
旁路逐位复制（含 NaN/负零），精确原地合法；
短载荷、部分重叠和 State 别名明确拒绝。
有效零帧事件仍重算位图并交付，下游错误不回滚已更新位图。
候选曲线上下限校验失败保留旧状态，探针禁止写入。

定向回归：`tests/piecewise_soft_clipper_smoke.c` 和
`orpheus_core/tests/test_piecewise_clipper_interfaces.py`。

## 来源

该曲线适用于需要在阈值上方平滑压缩并在输入上限饱和的多通道后处理链。
