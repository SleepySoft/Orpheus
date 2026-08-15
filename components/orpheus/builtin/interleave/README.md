# orpheus.builtin.interleave — 交错器

## 功能

把 N 路**单声道**输入合并为一路 **N 通道交错**输出（`out[f*N+ch] = in_ch[f]`）。

工作原理：逐通道遍历，把第 `ch` 路输入的每个样本写到输出帧的第 `ch` 个槽位；**未连接的输入引脚写 0（静音）**，不会残留脏数据。

本组件是 AWE 风格"**通道映射即连线**"的体现：没有独立的"映射表"参数，引脚 `in0..in(N-1)` 与交错输出的通道 0..N-1 一一对应——你把某条信号连到 `in3`，它就是输出的第 3 通道。想换通道顺序，改连线即可。

### 可变引脚

`channels` 参数决定输入引脚数量（AWE 风格可变引脚，`count: param:channels`）：改成 4，画布上立即出现 `in0..in3` 四个输入引脚。改 `channels` 属于改签名（`affects_signature`），需重新编译。

## 端口

| id | 方向 | 说明 |
|---|---|---|
| `in0..in(N-1)` | input | N 路单声道输入（编译期由 `in` × `param:channels` 展开），未连接的引脚按静音处理 |
| `out` | output | 一路 N 通道交错输出 |

## 参数

| 参数 | 类型 | 默认值 | 范围 | update_policy | 说明 |
|---|---|---|---|---|---|
| `channels` | int | 2 | 1~32 | restart_required（影响签名） | 通道数 = 输入引脚数 = 输出交错通道数 |

## 注意事项

- 输入引脚必须按 `in0..in(N-1)` 的索引理解：中间跳过的引脚对应通道输出静音，不会自动"压缩"通道序号。
- 与 `deinterleave` 互为逆操作：`deinterleave → interleave`（同 `channels`）可还原原信号。
- 仅做样本搬运，不改采样率、块长，零延迟。

## 典型用法

```
mic_left  ──► in0 ┐
mic_right ──► in1 ├─► interleave(channels=2) ──► out（立体声交错）──► device_out
```

把多个单声道处理链的末端汇成一路多通道设备输出；反向拆通道用 `deinterleave`。

## 实时安全

process 内无堆分配、无锁、无 IO，纯样本拷贝循环；`realtime_safe: true`。
