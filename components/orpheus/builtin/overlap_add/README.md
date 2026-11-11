# 重叠相加

`in` 是驱动入口，每次接收 `frame_size * num_frames` 帧交错f32载荷；
按 `hop_size` 逐帧重叠相加，保存尾部后显式交付1号 `out`，
每次输出 `hop_size * num_frames` 帧。
无水位等待、独立tick、静态分频或会合补调，采样率不变。

| 参数 | 默认值 | 范围 |
|---|---|---|
| channels | 2 | 1～64 |
| frame_size | 128 | 2～4096 |
| hop_size | 64 | 1～frame_size |
| num_frames | 12 | 1～256 |

参数均为restart_required且影响接口规格。
显式载荷独立于ctx处理量子，输入长度必须精确匹配；不截断、不隐式补齐。
逐帧按原float32顺序加到历史，再复制前hop_size帧、左移尾部并清零末尾。
不附加分析/合成窗、不归一化，也不执行FFT/IFFT。

输出继承实际输入批次的Delivery时间锚点及标志；
帧展开槽位不是连续时间采样数，不能从中反推Task周期。
DISCONTINUITY不自动清空算法历史；reset清空历史并保留配置。
零处理量子不推进、不交付；出口可悬空但历史仍推进。
下游失败不回滚历史，不在重试时重放同一尾部。

历史只在prepare分配，process无资源分配；
非法候选保留现有配置，分配失败清理后可重试。
输入/输出及与State/历史存储重叠明确拒绝。

[频域往返示例](../../../../examples/spectral_roundtrip.yaml) 使用
256帧长、128帧移、每次3帧及显式0.5归一化；
矩形窗口往返产生128帧算法延迟，不靠修改Delivery时间戳掩盖。
真实WAV回归验证动态/生成代码、global/Task四个入口逐字节一致，
相对独立延迟预期误差不超过2个PCM16量化单位。

[component.yaml](component.yaml) 为事实源，算法在 [user/algorithm.c](user/algorithm.c)：

```powershell
python scripts\generate_component.py components\orpheus\builtin\overlap_add\component.yaml
```
