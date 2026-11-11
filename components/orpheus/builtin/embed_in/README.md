# 嵌入式音频输入

`platforms: [dsp]` 的完整块输入边界，不包含 ADC/DMA IO 或内部定时器。
`clock_source: true`、`clock_domain: embed`；`sample_rate` 声明图速率。
唯一事实源是 [component.yaml](component.yaml)，独立脚本生成接口壳。

## 交付契约

- 宿主通过 `out` 的 `host_binding` 写入 `samples`，再提交 `ready=true`。
  生成工程使用公开 API，无需访问组件 State：

  ```c
  int rc = orpheus_audio_submit("adc", "out", dma_block, ORPHEUS_AUDIO_adc_out_SAMPLES);
  if (rc != ORPHEUS_OK) return rc;
  ```

- 提交只供数、不触发；外部推进 Task 后，组件消费一次完整块，先清 `ready`，
  再真实 `emit out(1)`。下游错误直接传播，不回滚消费、不补发。
- 缺块返回 `ORPHEUS_ERR_PROCESSING`，`underruns` 饱和累计到 `INT_MAX`。
  **不再重复旧块或自动补零**。需要明确静音时，平台主动提交完整零块。
- 零帧不消费、不交付；正帧必须等于配置帧数，通道数/格式/容量/采样率必须匹配，
  拒绝输出与组件私有存储别名。输出未连接时仍消费；`reset` 清数据、ready 与欠载探针。

## 规格与平台适配

`channels` 为 1..32，`sample_rate` 为 1000..192000 Hz；均为重启/签名参数。
有效存储数量是 `config:frames × channels`，上限 65536 个 f32 样本，
超限配置明确拒绝；物理私有数组为 256 KiB，启用双 bank 时另有影子存储。

[平台适配模板生成器](../../../../orpheus_core/orpheus_core/generator.py) 提供：

- `int orpheus_platform_io_init(void)` 初始化硬件；
- `int orpheus_platform_io_pre_block(const char* task)` 在所属 Task 前提交真实采集块；
- `int orpheus_platform_io_post_block(const char* task)` 读取输出。

`task=NULL` 表示全图入口，其它值为 Task ID；模板按节点所属 Task 分区。
pre 钩子成功后才提交本次 BULK，因此本次输入在本次 Task 中生效，不多延迟一块。
三个钩子的错误均传播到图错误信息。默认模板不供数、不伪造成功结果。
平台端口操作必须与 Task 串行；硬件中断应先交给平台缓冲，不能并发修改组件。
重新生成会覆盖模板，硬件实现请另存并合并。

动态 Runtime 同样通过公开宿主端口提交，不能再转换旧 `EmbedInState` 或注入 `src` 指针。
实时 process 仅检查、复制、更新探针与同步交付，无分配、锁或 IO。
