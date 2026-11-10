# 数组通道拆分

`in` 是 `channels × elements` 的交错 f32 数组，`out0..out(N-1)` 各含
`elements` 项。这里只搬运原始四字节位型，保留 NaN 载荷、无穷、负零和非规格数，
不做采样率转换，不把数组长度当作 Task 处理量子。

- `channels`：默认 2，范围 1..128；`elements`：默认 129，范围 1..65536。
  二者均为影响签名的重启参数。
- 配置是唯一事实源，独立脚本生成有界出口数组、实际 `out_count` 及局部编号
  `1 + 通道索引`。每路填好后立即显式交付，完整下游子树返回后再处理下一路。
- 在写入或交付前检查全部缓冲；拒绝错误尺寸/格式、平面布局、输入输出重叠、
  不同输出重叠。缺失输出跳过，零处理量子不写载荷。
- `in` 是处理入口；未连接的出口不调用任何下游，不增加会合等待或同拍去重。
- 这些是数据端口，不是参数 BULK 槽。状态内存由宿主 Arena 提供；
  process 中不分配、不阻塞、不做 IO。

```powershell
python scripts\validate_component.py components\orpheus\builtin\bulk_deinterleave\component.yaml
python scripts\generate_component.py components\orpheus\builtin\bulk_deinterleave\component.yaml
```
