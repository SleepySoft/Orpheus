# rate_sync

多速率异步合流（multi-rate async merge）同步缓冲组件。

## 用途

当两条（或多条）音频路径被各自降采样到**同一绝对采样率、同一 tick 块长**，
但来自不同基块速率/不同调度除数时，不能直接进一个普通 mixer（编译器会因
"rate mismatch" 或 "block size mismatch" 拒绝）。

`rate_sync` 通过 manifest 的 `scheduling.merge: true` 声明"接受异步多速率输入"，
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
- `out`：对齐后的合流输出（audio, f32）

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
