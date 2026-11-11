# 实数 IFFT

`in` 音频视图与 `spectrum` 半复数BULK视图必须且只能连接一个；
两者均为驱动入口。连接的全零谱仍是有效输入，不能被当成未连接。
每次接收 `fft_size * batches` 帧，按每帧N槽半复数布局还原实数音频，
由算法显式交付1号 `out`，含 `1/N` 归一化。

`fft_size` 默认256，必须为4～1024之间的2次幂；
`batches` 默认1，范围1～64；通道数1～32，默认2。
均须重启后生效。点数/批次显式声明，不再使用旧 `fft_size=0` 自动推断。
输入BULK的halfcomplex/frame_size标签必须匹配，不做隐式布局转换。

保留原Hermitian展开、共轭FFT及缩放顺序；每批各自转换，无跨批滤波历史。
显式载荷独立于ctx处理量子，继承实际Delivery时间和质量标志，
不以Task调用次数推断输入时间，不加等待或同拍去重。
零处理量子不计算、不交付；悬空输出仍可完成调用。
输入/输出及与State的存储重叠拒绝，process不分配资源。
下游错误直接返回，不回滚已计算结果。

事实源为 [component.yaml](component.yaml)，手写算法位于
[user/algorithm.c](user/algorithm.c)，独立生成命令：

```powershell
python scripts\generate_component.py components\orpheus\builtin\ifft\component.yaml
```

原生测试验证两种输入视图、互斥/缺失/容量保护、独立DFT往返误差及错误传播；
图级测试通过带标签BULK路径验证动态/生成代码的global/Task四个入口。

## 外部模型映射

对应FDP的实数逆变换原语，采用 `conj(FFT(conj(X)))/N`。
本组件只做逆变换，不包含overlap-add；重建可连接独立overlap_add组件。
