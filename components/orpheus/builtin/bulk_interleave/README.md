# 数组通道合并

`in0..in(N-1)` 各含 `elements` 项，`out` 是 `channels × elements` 的交错
f32 数组。原始四字节位型逐项复制，未连接输入填正零，不压缩通道编号。

- `channels`：默认 2，范围 1..128；`elements`：默认 129，范围 1..65536。
  二者均为影响签名的重启参数。
- 每个输入均为处理入口；每次实际交付立即读取当前可见的所有输入、合并并交付出口 1。
  其他已连接输入可能仍是上一次内容，不等待会合、不按同拍去重。
- 数组尺寸独立于 Task 量子：例如 Task 每次推进 8 帧，可以搬运每通道 7 或 13 项；
  `ctx.frame_count/valid_frames` 仍描述处理量子，Buffer 描述载荷。
- 拒绝错误尺寸/格式、平面布局及输入输出重叠；缺失输入填零，缺失输出不交付，
  零处理量子不写载荷。
- 独立脚本从接口配置生成有界输入视图和真实 `input_index`；
  状态由宿主 Arena 提供，process 无分配、锁或 IO。

```powershell
python scripts\validate_component.py components\orpheus\builtin\bulk_interleave\component.yaml
python scripts\generate_component.py components\orpheus\builtin\bulk_interleave\component.yaml
```
