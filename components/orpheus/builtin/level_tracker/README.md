# dB电平保持衰减

声明文件是接口事实源，独立生成入口：

```powershell
python scripts\generate_component.py components\orpheus\builtin\level_tracker\component.yaml
```

`peak` 是唯一驱动入口，`rms`、`volume`、`fast_trigger`、`volume_gain`
仅供算法读取当前可见数据，不触发、不等待、不补调。双dB检测器先准备两份统计，
再依次交付 RMS 和峰值；峰值事件驱动保持器一次。输入缺失使用对应参数。
输入快速触发口改名为 `fast_trigger`，避免与参数 `trigger` 共用接口标识。

每次非空调用更新一次双精度保持计数，不按音频帧数重复递推。保持严格超过
`hold_calls` 才结束，下降不足裕量仅清计数、不重新激活保持。冻结不推进历史，
输出声压级为零；冷启动仍先建立 `-abs(min_db)` 包络。
线性音量显式转换为 dB，与 dB 音量同时连接报错。保留原 f32 次序及 NaN 比较语义。

两份输出先填完，再由算法按 `level(1)` → `envelope(2)` 交付；
1帧标量载荷不改变事件时间量子。下游失败立即停止后续出口，不回滚历史。
未连接输出允许为空，零帧不推进、不交付旧值；reset 清历史并保留配置。
三个状态探针只读；输出不得与输入、实例状态或另一输出重叠。
