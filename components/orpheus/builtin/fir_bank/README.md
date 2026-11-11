# fir_bank — 多通道FIR滤波器组

接口声明是唯一事实源；独立CLI生成类型化壳，算法每次接收真实 `in`
交付时执行，完成所有通道并提交历史后显式 `emit out(1)`。
同拍重复到达分别处理，不补调、不去重；下游失败不回滚已处理历史。
输出保留实际输入交付的时间锚点、epoch和质量标志。

```powershell
python scripts\generate_component.py components\orpheus\builtin\fir_bank\component.yaml
```

## 配置与资源

音频为交错f32，`channels`为1～64，`num_taps`为1～4096。
三种系数模式只导出当前模式的双缓冲槽：

| coefs_mode | 槽键 | 实际数量与布局 |
|---|---|---|
| shared | coefs | num_taps，所有通道共享 |
| per_channel | coefs_per_channel | channels×num_taps，通道→抽头 |
| banked | coefs_banked | num_filters×num_taps，系数集→抽头 |

逐通道/系数集模式最多32768个系数，shared最多4096个。
未启用模式不做容量运算、不注册槽、不生成数据点ID；
因此shared的64通道×4096抽头合法，不被未启用的逐通道资源误拒绝。
算法用互斥union复用系数存储，不为三种模式各分配一份系数数组。

`coefs`是所有模式通用的初始CSV，系数从最新样本到最旧样本排列。
per_channel/banked也可分别通过 `initial_coefs_per_channel` /
`initial_coefs_banked` 提供初值；非空时覆盖coefs。
空CSV为每组单位脉冲，非空时严格提供完整的有限f32系数。
每个初值字符串最多65535字节；完整大数组可通过运行期BULK写入。

`taps_per_channel`空值表示所有通道使用num_taps；
非空则严格提供channels个0～num_taps整数。
0抽头通道输出正零且不读输入、不更新自身历史。
有效历史环长是实际抽头数，但物理系数/历史跨度仍为num_taps；
未使用的系数即使在BULK更新后含NaN也不读取。

banked另导出 `filter_indices_i32`：
channels项int32，双缓冲、块边界更新、可绑定/读取，
每项必须为0～num_filters-1；切换系数集不清历史。
初始CSV `filter_indices` 非空时设置该数组，否则使用整数数组初值。
CSV索引与异构抽头数可以同时配置；运行期使能切换不会重新应用初始CSV。

## 运算与生命周期

每个通道每个样本保留原反向累加：

```text
sum = 0
for k = 实际抽头数-1 ... 0:
    sum += coefs[k] * history[当前样本之前k个样本]
```

不改为正向点积或其它加法分组，保留f32舍入行为。
`processing_enabled=0`输出正零且冻结所有历史/游标，不读取输入；
重新启用从旧历史继续。reset只清历史和游标，保留系数、索引和使能。
允许完整原地处理，拒绝部分重叠和私有系数/历史别名。
零量子不改变历史也不emit。

历史只在prepare分配、destroy释放；实时处理不分配内存。
非法配置在接触旧资源前拒绝；分配失败清理后可以重新prepare。
