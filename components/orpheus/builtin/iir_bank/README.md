# iir_bank — 级联二阶滤波器组

接口声明是唯一事实源；独立CLI生成类型化壳，算法只在每次真实 `in`
交付时执行，完成所有通道并提交历史后显式 `emit out(1)`。
同一Task拍内多次到达分别处理，不按Task计数去重；下游失败不回滚已处理历史。
`out` 保留实际输入交付的时间锚点、epoch和质量标志。

```powershell
python scripts\generate_component.py components\orpheus\builtin\iir_bank\component.yaml
```

## 接口

| 接口 | 内容 |
|---|---|
| `in` | audio f32，`channels`通道，`process: true` |
| `out` | 同规格音频，`drive_port_no: 1` |
| `channels` | 1～64，默认2，重启更新 |
| `num_stages` | 1～16，默认4，重启更新 |
| `stages_per_channel` | 空值沿用统一级数；否则严格提供每通道0～num_stages项数 |
| `coefs_mode` | shared或per_channel，默认shared，重启更新 |
| `coefs` | 初始紧凑CSV，默认`1,0,0,0,0`，每节按`b0,b1,b2,a1,a2` |
| `initial_coefs_per_channel` | per_channel可选初值，非空时覆盖coefs |
| `processing_enabled` | 0/1，默认1；立即更新、可读、可绑定 |

初始CSV按“通道→有效配置级数→五项”紧凑排列；只提供前缀时，
未提供位置保持单位节。空CSV全部为单位节。CSV格式错误、越界或数值溢出明确拒绝，
不再跳过任意垃圾字符；字符串容量为65536字节，包含NUL。
系数支持IEEE f32非有限值与非零次正规数；不执行的节即使含NaN也不读取。

0级通道逐位复制输入，不执行任何节、不更新其历史。
停用时输出正零且不读取输入、不更新任何历史；重新启用从旧历史继续。
reset只清历史，不改变使能、系数和每通道级数。
零量子不改变历史也不emit。组件不支持原地或部分重叠输入/输出。

## BULK系数

只注册当前模式对应的槽，两种模式分别拥有私有存储：

| 模式/槽键 | 数量 | 布局 |
|---|---|---|
| shared / `coefs` | 80 f32 | 16节×5项 |
| per_channel / `coefs_per_channel` | channels×80 f32 | 每通道固定80项跨度 |

每通道只执行 `stages_per_channel` 指定的前若干节；
紧凑CSV与BULK的固定80项跨度不同，不能混用。
完整BULK写入双缓冲区，下一次处理入口切换；系数更新不清历史。

## 数学

每节为Direct Form II Transposed，保留原f32运算顺序：

```text
y  = b0*x + z1
z1 = b1*x + z2 - a1*y
z2 = b2*x - a2*y
x  = y
```

每通道有独立的z1/z2历史。它是可配置级数的二阶节级联，
不是直接型多项式 `iir`，也不是固定两节的 `biquad_bank`。

## 外部模型映射

| 外部模型源码 | 本组件 |
|---|---|
| pooliir GLXP IIR加速器的首级gain,a2,a1,b2,b1和后续a2,a1,b2,b1 | 转为每节`[b0,b1,b2,a1,a2]` |
| `pooliirAccelerator.h` / `pooliirAccel` CRL | 软件级联循环，无GLXP硬件加速 |
| SAS_PeripheralEq / PostEQ / HoligramIir | 按各自级数与通道系数配置 |
| MixEqpooliirCoeffs / OutputEQpooliirCoeffs | 对应模式的BULK槽 |
