# orpheus.builtin.embed_out — 嵌入输出

## 功能

**嵌入式部署路径专用的"平台数据出口"占位节点**（`platforms: [dsp]`）。在图上它代表"处理结果从这个点离开图，交给硬件 DAC / DMA"。组件本身不做真实 IO：`process` 只把输入 buffer 拷贝到一块**用户消费的内存区**（`state->dst`）。

工作原理（代码生成路径）：`cli generate` 为每个 `embed_out` 节点生成：

- 全局缓冲 `float g_embed_out_<节点>[块长 × channels]`；
- 状态访问器 `EmbedOutState* orpheus_embed_out_state_<节点>(void)`；
- `platform_io.c` 模板：你在 `orpheus_platform_io_post_block()`（每块处理后）把 `g_embed_out_*` 交给 DAC/DMA。

组件每块把一整块输入拷贝进 `dst`；**若 `dst` 为 NULL 或容量不足一整块，该块被静默丢弃**（不报错、不部分拷贝）。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | 待送出平台的音频（interleaved f32），`channels` 通道，采样率 = `sample_rate` |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels` | int | 2 | 通道数，1~32；应与上游 `embed_in` 的通道配置一致。affects_signature，restart_required |
| `sample_rate` | int | 48000 | 嵌入 I/O 采样率（Hz），1000~192000；**连接校验要求与图的时钟源（embed_in）一致**。affects_signature，restart_required |

### `sample_rate` 为什么必须和 `embed_in` 一致

`embed_out` 不是时钟源，它消费的是图时钟驱动的数据。声明不同的采样率没有意义且会造成速率错配，因此编译期连接校验直接强制两者相等——改采样率请改 `embed_in` 一侧（它是源驱动全图的时钟源）。

## 注意事项

- 这是**代码生成路径**的组件：PC 实时路径请用 `device_out`。
- 数据"送出"的终点是内存缓冲，不是硬件——真正把数据搬给 DAC/DAC DMA 的代码由你在 `platform_io.c` 的 `post_block` 钩子里实现。
- 丢弃不告警：若你没在 `post_block` 消费数据或消费侧跟不上，组件层面看不出异常（没有 overrun 探针）；调试时可先确认 `post_block` 确实被调用。
- 输入引脚未连接时 `process` 直接返回错误（返回 `ORPHEUS_ERR_INVALID_ARG`），请保证它始终有上游。
- 重新运行 `cli generate` 会覆盖 `platform_io.c`，填好的 USER CODE 请另存副本或手工合并。

## 典型用法

```yaml
component: orpheus.builtin.embed_out
params:
  channels: 2
  sample_rate: 48000        # 必须与 embed_in 一致
```

```
embed_in ──► 处理链 ──► embed_out          # 嵌入式主链：ADC → 算法 → DAC
```

生成工程后在 `platform_io.c` 的 `post_block` 里：

```c
dac_write(g_embed_out_sys_out, frames * out->channels);
```

## 实时安全

manifest 标记 `realtime_safe: true`：`process` 只有一次 memcpy（条件不满足时零操作），无分配、无锁、无 IO。
