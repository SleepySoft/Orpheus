# indexed_gain - 索引式增益

将 0..255 等外部索引通过自定义 dB 表转换为共享线性增益，并以逐样本指数 ramp 作用到多通道音频。支持 dB offset、最大增益、freeze、dB/s 或 ms 时长，以及独立的冷启动线性增益。

`apply_index_on_prepare=true` 时按索引初始化；设为 false 时使用 `initial_gain`，直到首次 `gain_index` 更新。`current_gain` 是标准只读控制源，可供后续控制链读取。

适用于外部模型中的表驱动 Volume；不绑定具体表或通道布局。
