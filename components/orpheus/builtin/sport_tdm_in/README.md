# SPORT/TDM 输入

`platforms: [adsp21593]` 的 SPORT/TDM DMA slot 输入边界，不包含 DMA IO 或内部定时器。
物理 slot 映射和格式（q1_31 / s24 左/右对齐）由工程顶层 `sport_bindings` 声明；
`clock_source: true`；`sample_rate` 声明图速率。
唯一事实源是 [component.yaml](component.yaml)，独立脚本生成接口壳。

## 交付契约

- 生成的 SPORT adapter 解码 slot 后经 `out` 的 `host_binding` 提交完整块：

  ```c
  int rc = orpheus_audio_submit("sport_in", "out", dma_block,
      ORPHEUS_AUDIO_sport_in_out_SAMPLES);
  if (rc != ORPHEUS_OK) return rc;
  ```

  不再需要（也不再提供）组件私有 State 的 `src/src_frames` 指针。
- 提交只供数、不触发；外部推进所属 Task 后，组件消费一次完整块，先清 `ready`，
  再真实 `emit out(1)`。下游错误直接传播，不回滚消费、不补发。
- 缺块返回 `ORPHEUS_ERR_PROCESSING`，`underruns` 饱和累计到 `INT_MAX`。
  **不再重复旧块或自动补零**（旧版会补零并继续，已废除）。需要明确静音时，
  平台主动提交完整零块。
- 零帧不消费、不交付；正帧必须等于配置帧数（Task 整块触发），
  通道数/格式/容量/采样率必须匹配，拒绝输出与组件私有存储别名。
  输出未连接时仍消费；`reset` 清数据、ready 与欠载探针。

## 规格

`channels` 为 1..64，`sample_rate` 为 1000..384000 Hz；均为重启/签名参数。
`clock_domain` 为节点级时钟域标识，供编译器做时钟链分组，组件不解释其取值。
有效存储数量是 `config:frames × channels`，上限 65536 个 f32 样本，
超限配置明确拒绝；物理私有数组为 256 KiB，启用双 bank 时另有影子存储。

生成 adapter 的固定顺序是：

```text
DMA RX -> slot gather / 定点转 f32 -> 提交 host_binding -> 推进 Task -> 图处理
```

触发上下文（`epoch/frame_index/sequence`）由 Task 入口透传为 Delivery 时间线；
`sequence_errors`/`frame_errors` 等触发统计是契约违约观测，不替代硬件锁相证明。
