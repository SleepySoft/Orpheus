# orpheus.builtin.rate_sync — 双路缓冲合流

同采样率音频的双路 FIFO 缓冲合流，对齐读取后逐样本相加；不执行采样率转换。
本组件不提供跨线程 SPSC 同步，不能与 `async_bridge` 的异步任务边界混为一谈。

## 用途

当两条音频路径处于**同一绝对采样率**、但交付块长不同，不能直接进普通
mixer（编译器会因 "rate mismatch" 或 "block size mismatch" 拒绝）。

`rate_sync` 通过 manifest 的 `scheduling.merge: true` 声明允许多速率块长合流，
编译器据此放行跨除数合流，把本节点的输出块长设为各输入块长的 **LCM（最小公倍数）**，
并把两条输入边标记为 rate-bridge：生产者写各自 staging，运行时按写游标滚入
深度为 LCM 的桥接 buffer，本组件在同步点以整块读取。

## 端口与驱动

- `in0` / `in1`：两组输入（audio, f32, 通道 = `channels`），均不驱动；
  由静态调度表在同步点整点调用本节点，每次两路各交付 `ctx.frame_count`（= LCM）帧。
- `out`：两路对齐后相加的音频（audio, f32），`drive_port_no: 1`；
  每次调用显式交付一次，未攒够对齐窗口时交付 0 帧。

组件内部每路一个 FIFO（容量 = 对齐窗口 + 一个合流块，容纳跨调用余量）：
每次调用先把两路整块追加进各自 FIFO，两路都达到对齐窗口后按窗口整块读出相加。
不是选择一路，也不是通道拼接。

## 参数

| 参数 | 类型 | 说明 |
|---|---|---|
| `channels` | int | 通道数（各输入/输出一致），默认 1 |
| `mode` | int | `0`=自动（默认，对齐窗口 = 合流块长），`1`=固定（用 `buffer_length`） |
| `buffer_length` | int | `mode=1` 时手动指定对齐窗口（帧），`0`=自动；**必须不小于合流块长**，否则准备期拒绝 |

三项均为 `restart_required` 参数，不能运行期修改（宿主重启实例生效）。

## 不变量与错误

- 任一 FIFO 装不下本次整块（显式窗口小于合流块长）在**准备期**拒绝，不留到运行期丢数据。
- 单块帧数超过 FIFO 容量、输入/输出与 State 或 FIFO 别名在拷贝前拒绝，不部分写入。
- 溢出前置拒绝、状态不变；下游交付失败不回滚已消费数据；零帧调用不推进、不交付。
- `reset` 清空两路 FIFO 与游标，配置保留。

## 示例

```yaml
nodes:
  - id: sync
    component: orpheus.builtin.rate_sync
    params: { channels: 2, mode: 0, buffer_length: 0 }
connections:
  - from: ee_slow:out
    to: sync:in0
  - from: rnc_slow:out
    to: sync:in1
  - from: sync:out
    to: analysis:in
```

## Realtime 红线

`process` 内不 malloc/free、无阻塞锁、无 IO，仅按 FIFO 对齐，实时安全；
FIFO 内存在 prepare 分配、destroy 释放。
