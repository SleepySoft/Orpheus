# orpheus.builtin.mapped_fir_bank - 映射 FIR 滤波器组

将多个输入通道映射到任意数量的 FIR filter，再按 `output_starts` 把连续 filter 分组求和。适合由硬件 FIR accelerator 生成的“输入少、filter 多、输出按组汇聚”模型。

## 参数

- `filter_lengths[f]`：filter `f` 的抽头数。
- `coefficient_mapping[f]`：filter `f` 使用哪个系数集。
- `input_mapping[f]`：filter `f` 读取哪个输入通道。
- `output_starts[o]`：输出 `o` 对应的首个 filter；下一个起点之前的 filter 全部求和。
- `coefficients`：按系数集顺序连续排列的 float 数组。

所有数组在 `prepare` 校验并分配，`process` 内无分配、锁和 IO。系数数组建议通过目录工程 `$resource` 的 `f32le` 文件提供。

## 延迟与内存

实例算法延迟取决于非零 tap 和 `output_delay_samples`，不能由组件 manifest 的固定值表达。若最后非零 tap 为 `K`（零起始），总延迟为 `K + output_delay_samples`。

prepare 动态内存（不计固定 metadata）约为：

```text
4 * (coefficient_count + input_channels * max_taps
	+ output_channels * output_delay_samples) bytes
```

BAF Headrest 默认实例约 106320 bytes，Overhead 默认实例约 84832 bytes。分配只发生在 prepare，实时 process 不分配。