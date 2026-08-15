# orpheus.builtin.embed_in — 嵌入输入

## 功能

**嵌入式部署路径专用的"平台数据入口"占位节点**（`platforms: [dsp]`）。在图上它代表"硬件 ADC / DMA 送来的音频从这张图的这个点进入"。组件本身不做任何真实 IO：`process` 只从一块**用户填充的内存区**（`state->src`）把样本拷贝到输出端口。

它是 `clock_source`（`clock_domain: embed`），且 `sample_rate` 参数会**源驱动**整张图的编译期采样率——嵌入式工程里没有"工程全局采样率"替你兜底，图的速率就由它声明。

工作原理（代码生成路径）：`cli generate` 生成独立 C 工程时，为每个 `embed_in` 节点生成：

- 全局缓冲 `float g_embed_in_<节点>[块长 × channels]`（你的 DMA 可以直达）；
- 状态访问器 `EmbedInState* orpheus_embed_in_state_<节点>(void)`；
- 适配模板 `platform_io.c`，含三个 USER CODE 钩子：
  - `orpheus_platform_io_init()`：一次性初始化（配 DMA/编解码器）；
  - `orpheus_platform_io_pre_block()`：**每块处理前**把采集数据写进 `g_embed_in_*` 并设置 `src_frames`；
  - `orpheus_platform_io_post_block()`：每块处理后（配合 `embed_out` 使用）。

每块调度时组件从 `src` 拷贝 `src_frames` 帧到输出；不足块长的部分**补零并累计 `underruns`**。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| out | output | audio | 平台输入音频（interleaved f32），`channels` 通道，采样率 = `sample_rate` |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels` | int | 2 | 通道数，1~32。affects_signature，restart_required |
| `sample_rate` | int | 48000 | 嵌入 I/O 采样率（Hz），1000~192000；**同时成为整张图的时钟源采样率**。affects_signature，restart_required |
| `underruns` | int | 0 | 欠载次数（readback 探针，只读）：用户供给不足一满块的次数 |

### `underruns` 探针：供给是否跟上的体温计

每次 `process` 时若 `src` 为 NULL 或 `src_frames` 小于块长，缺失部分补零且计数 +1。正常运行中它应该恒为 0；持续增长说明你的 `pre_block` / DMA 供给跟不上图的消费节奏（或忘了设置 `src_frames`）。

## 注意事项

- 这是**代码生成路径**的组件：PC 实时路径请用 `device_in`。图上用它标记的图应整体面向嵌入式部署。
- `sample_rate` 没有"0 = 继承"的选项——必须显式给出（1000~192000 Hz），因为它就是图的时钟源。
- 下游若接 `embed_out`，其 `sample_rate` 必须与本组件一致（连接校验强制）。
- `reset` 会把 `src_frames` 清零（`underruns` 只在 prepare 清零）：如果重置后第一块之前没人重新供给，会记一次欠载。
- 重新运行 `cli generate` 会覆盖 `platform_io.c`，填好的 USER CODE 请另存副本或手工合并。

## 典型用法

```yaml
component: orpheus.builtin.embed_in
params:
  channels: 2
  sample_rate: 48000
```

```
embed_in ──► 处理链 ──► embed_out          # 嵌入式主链：ADC → 算法 → DAC
```

生成工程后在 `platform_io.c` 的 `pre_block` 里：

```c
memcpy(g_embed_in_sys_in, dma_rx_buf, frames * in->channels * sizeof(float));
in->src_frames = frames;
```

## 实时安全

manifest 标记 `realtime_safe: true`：`process` 只有 memcpy/memset，无分配、无锁、无 IO——数据在哪、谁填的，组件完全不关心，这正是它能在裸机/DMA 中断环境里使用的原因。
