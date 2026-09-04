# orpheus.builtin.baf_soft_clipper - BAF 分段软削波

## 功能

逐样本实现 BAF `PostProcess/Soft Clipper/SoftClipper/MATLAB Function` 的二次分段曲线：

$$
x_1=\min(|u|,x_{max}),\quad x_2=\max(x_1-x_{min},0),\quad
y=\operatorname{sign}(u)(x_1-p_2x_2^2)
$$

它与通用 `soft_clipper` 的 tanh 曲线不同，因此作为模型专用组件独立存在。

## 参数

- `xmin/xmax/p2`：高档参数。EREV-1 Model 1.1 默认 `0.65 / 1.35 / 0.714285731`。
- `xmin_low/xmax_low/p2_low`：低档参数，当前参考模型默认与高档相同。
- `param_set`：1 选择高档，其他值选择低档。
- `disabled`：直通，不执行软削波。
- `active_mask`：本块中峰值超过 `xmin` 的通道位图，只读探针。

## 来源

公式来自 EREV-1 BAF out 生成文件 `rt_sys_PostProcess_87.c` 中的 `Model_1_1_MATLABFunction`。TOP 默认值来自 `Model_1_1_PostProcess_p0_b0_TOP.c`。
