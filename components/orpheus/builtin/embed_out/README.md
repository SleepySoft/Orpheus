# 嵌入式音频输出

`platforms: [dsp]` 的完整块输出边界，不包含 DAC/DMA IO。
唯一事实源为 [component.yaml](component.yaml)，接口由独立脚本生成。

## 交付契约

- `in` 是唯一驱动入口（`process: true`）。每次真实到达复制完整交错 f32 输入到
  `samples`，再置 `valid=true`；这是链尾，不自动调用其它组件。
- 宿主通过 `in` 的 read 绑定读取最新完整块：

  ```c
  int rc = orpheus_audio_read("dac", "in", output, ORPHEUS_AUDIO_dac_in_SAMPLES);
  if (rc != ORPHEUS_OK) return rc;
  ```

  成功读取后清 valid；重复读取在新块到达前明确报错，不再次消费旧数据。
  多次组件调用保留最后一块，不等待宿主、不增加队列或会合语义。
- 正帧输入必须存在，帧数等于配置帧数，格式/通道数/容量/采样率必须匹配；
  错误入口、短输入和与私有存储别名均在修改数据前拒绝。
  **不再因 dst 缺失/容量不足而静默丢弃**，也不依赖宿主注入私有指针。
- 零帧不更新数据或 valid；reset 清缓冲及 valid。

## 规格与平台适配

`channels` 为 1..32，`sample_rate` 为 1000..192000 Hz；均为重启/签名参数。
采样率必须与上游时钟一致，不执行采样率转换。
有效存储为 `config:frames × channels`，最多 65536 个 f32 样本；
私有物理数组为 256 KiB，双 bank 另有影子存储，超限配置明确拒绝。

生成工程在 `orpheus_platform_io_post_block(const char* task)` 中通过公开 read API
交给 DAC；返回错误会传入图错误信息，不冒充成功。模板按 Task ID 分区，
`NULL` 表示全图入口，默认不读取/丢弃输出。
生成的图静态库和动态 Runtime 使用相同 host_binding，不再导出旧 State 访问器。
硬件读写须与 Task 串行；重新生成会覆盖平台模板，集成实现请另存并合并。

实时 process 只有输入校验、memcpy 和 valid 更新，无分配、锁或 IO。
