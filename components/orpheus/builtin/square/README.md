# orpheus.builtin.square — 逐样本平方

## 功能

最简单的逐样本数学运算组件：把每个样本平方，`output = input * input`。常用于把信号转成“瞬时功率”，再接入 `psd`、`coherence_matrix` 或 `interp_lut` 做进一步分析。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels` | int | 2 | 通道数，改变后需重新编译。 |

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，通道数与输入相同。

## 注意事项

- 平方会把负样本变成正数，输出始终非负。
- 输入幅度接近 1.0 时，输出接近 1.0；输入为 0.5 时输出为 0.25，动态范围被压缩。
- 实时安全，无状态，每个样本独立计算。

## 典型用法

```
fdp_signal ──► square ──► saturation(limit=0.5) ──► sink
```

在 Audiopilot 相干求和链路中，先对信号平方得到功率，再限幅，最后汇总。

## 配置驱动接口（ABI v7）

`component.yaml` 的 `interfaces` 是唯一接口事实源；修改后运行
`python scripts\generate_component.py components\orpheus\builtin\square\component.yaml`。
脚本管理 `generated/` 的元数据与 ABI 壳；`user/algorithm.c/.h` 保留手写逐样本平方。
`in` 是唯一驱动输入；全部输出样本计算完成后，在算法内显式 `emit_out`，
稳定出口编号为 `1`，通过 delivery 交付，不等待会合、不去重。支持原地计算。

参数按声明严格校验；越界或错误类型返回 `ORPHEUS_ERR_INVALID_ARG`，
不再隐式 clamp/类型转换。`channels` 是重启参数，不可运行期更新。
