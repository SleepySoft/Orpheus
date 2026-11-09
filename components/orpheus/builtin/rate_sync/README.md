# rate_sync — 双路缓冲合流

同采样率音频的双路FIFO缓冲合流，对齐读取后逐样本相加；不执行采样率转换。
本组件不提供跨线程SPSC同步，不能与`async_bridge`的异步任务边界混为一谈。

## 用途

当两条音频路径处于**同一绝对采样率**，但交付块长不同，
不能直接进一个普通mixer（编译器会因
"rate mismatch" 或 "block size mismatch" 拒绝）。

`rate_sync`通过manifest的`scheduling.merge: true`声明允许多速率块长合流，
编译器据此放行跨除数合流，并把本节点的输出块长设为各输入块长的 **LCM（公倍数）**，
由组件内部的 FIFO 在公共 tick 上做时间对齐后合流输出。

## 参数

| 参数 | 类型 | 说明 |
|---|---|---|
| `channels` | int | 通道数（各输入/输出一致），默认 1 |
| `mode` | int | `0`=auto（默认，缓冲长度=LCM），`1`=fixed（用 `buffer_length`） |
| `buffer_length` | int | `mode=1` 时手动指定缓冲长度（帧），默认 0 |

## 端口

- `in0` / `in1`：两组输入（audio, f32, 通道= `channels`）
- `out`：两路对齐后相加的音频（audio, f32），不是选择一路或通道拼接

## 示例 Diff

```yaml
components:
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

`process` 内不 malloc/free、无阻塞锁、无 IO，仅按 FIFO 对齐，实时安全。
