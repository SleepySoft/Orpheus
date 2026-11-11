# 实数 FFT

`in` 是驱动入口，每次接收 `fft_size * batches` 帧交错f32音频。
`fft_size` 默认256，必须为4～1024之间的2次幂；
`batches` 默认1，范围1～64；通道数1～32，默认2。
点数及批次是显式接口规格，不再使用旧 `fft_size=0` 或从Task量子推断单帧大小。
全部参数均须重启后生效。

每批独立执行原有radix-2 FFT，保留float twiddle计算与蝶形运算顺序。
两出口长度与输入相同，全部结果写完后依次显式交付：

1. `out`：音频视图，按output_mode选择halfcomplex、magnitude或power。
   幅度/功率均对称展开至N槽，不做归一化。
2. `spectrum`：带halfcomplex/frame_size标签的BULK计算数据，
   始终为未归一化复数谱，不受output_mode影响。

半复数布局：`hc[0]=Re(0)`、`hc[N/2]=Re(N/2)`、
`hc[k]=Re(k)`、`hc[N-k]=Im(k)`；批次顺排、通道交织。
它不是Re/Im交织布局，也不对DC/Nyquist虚部占用额外槽位。

显式载荷不改变ctx处理量子，输出继承实际Delivery的时间和质量标志；
不生成批次分频、等待或补调。第一个出口失败即停止，不再交付第二个出口。
零处理量子不计算、不交付；出口可独立悬空。
输入/输出、两个输出及与State的存储重叠明确拒绝。
表与暂存均在State内，process无资源分配；reset无跨调用信号历史需要清理。

事实源为 [component.yaml](component.yaml)，手写算法位于
[user/algorithm.c](user/algorithm.c)。使用独立脚本生成：

```powershell
python scripts\generate_component.py components\orpheus\builtin\rfft\component.yaml
```

原生测试保留独立DFT误差、逆变换回环、容量与布局数值基线，
并检查实际出口顺序、错误传播、别名和非法参数。
图级测试覆盖三种输出模式、最大点数/批次/通道、同拍多次调用及四种执行入口，
以独立NumPy频谱预期校验，而非只靠FFT/IFFT相互抵消误差。
测试宿主在处理返回后读回全部输出，并逐槽比较四入口位型完全一致。

## 外部模型映射

FDP的 `rfft_process_inplace` 与Audiopilot35频域分析使用相同变换原语。
此组件逐通道独立执行radix-2；未采用SHARC将两路实序列打包为一路复FFT的优化，
不能据此宣称性能或目标硬件实现等价。
