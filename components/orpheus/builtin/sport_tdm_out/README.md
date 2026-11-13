# orpheus.builtin.sport_tdm_out — SPORT/TDM 输出

`platforms: [adsp21593]` 的 SPORT/TDM DMA slot 输出边界。组件本身不启动 SPORT 传输，
也不配置 SPORT、DAI/SRU、PCG 或 DMA；它把图内交错 `f32` 结果交给生成的 adapter，
再由 adapter 完成格式转换和 slot scatter。`sample_rate` 声明图速率。
唯一事实源为 [component.yaml](component.yaml)，接口由独立脚本生成。

当前实现和验证目标是 `adsp21593`（ADSP-21593）。该实现模式对应 ADI SHARC/SHARC+
21xxx 的 full-duplex SPORT、TDM slot、外设 DMA 与 DAI/SRU 边界；
不能据此宣称所有 ADI DSP 都已支持。

## 交付契约

- `in` 是唯一驱动入口（`process: true`）。每次真实到达复制完整交错 f32 输入到
  `samples`，再置 `valid=true`；这是链尾，不自动调用其它组件。
- 生成的 SPORT adapter 在推进所属 Task 后经 `in` 的 read 绑定读取最新完整块：

  ```c
  int rc = orpheus_audio_read("sport_out", "in", dma_block,
      ORPHEUS_AUDIO_sport_out_in_SAMPLES);
  if (rc != ORPHEUS_OK) return rc;
  ```

  成功读取后清 valid；重复读取在新块到达前明确报错，不再次消费旧数据。
  不再需要（也不再提供）组件私有 State 的 `dst/dst_capacity` 指针。
- 正帧输入必须存在，帧数等于配置帧数（Task 整块触发），格式/通道数/容量/采样率
  必须匹配；错误入口、短输入和与私有存储别名均在修改数据前拒绝。
  **不再因 dst 缺失/容量不足而静默丢弃旧数据**。
- 零帧不更新数据或 valid；reset 清缓冲及 valid。

## 规格与顶层声明

`channels` 为 1..64，`sample_rate` 为 1000..384000 Hz；均为重启/签名参数。
`clock_domain` 为节点级时钟域标识，供编译器做时钟链分组，组件不解释其取值。
物理 SPORT resource、TDM slot 总数、slot 到逻辑通道的映射、定点格式在工程顶层
`sport_bindings` 中声明；块长、时钟域和触发组在顶层 Task/调度契约中声明。
编译器据此静态展开 scatter 和格式转换。有效存储数量是
`config:frames × channels`，上限 65536 个 f32 样本。

生成 adapter 的固定顺序是：

```text
推进 Task -> 图处理 -> read host_binding -> f32 转定点 / slot scatter -> DMA TX
```

触发上下文（`epoch/frame_index/sequence`）由 Task 入口透传；
`sequence_errors`/`frame_errors` 等触发统计是契约违约观测，不替代硬件锁相证明。
