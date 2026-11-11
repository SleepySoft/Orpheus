# 逐样本增益包络

`in` 是唯一驱动入口；`start/target/alpha/freeze` 是当前可见数据，
不因它们更新而调用算法，也不等待它们同拍到达。
每次音频交付以 start 为起点，按原始 f32 次序执行
`(1-alpha)*target + alpha*gain`，无跨调用状态；
freeze 非零时直接保持 start，不钳制 alpha。

算法先完成三个结果，再依次显式交付：

1. `out`（1 号）：同速率逐样本包络。
2. `gains`（2 号）：每通道末样本增益，载荷长度为 1，
   不改变调用的时间量子。
3. `audio`（3 号）：原音频逐位透传。

顺序由算法调用点决定，不由编号推导；下游错误立即返回，不继续后续出口。
有效零帧事件不重算结果，只交付仍存在的出口缓冲。
非零调用要求音频实际帧长恰等于时间量子，其余输入恰为 1 帧；
所有输入及出口容量由生成壳校验。支持未连接出口，
但不支持输入输出重叠、出口互相重叠或覆盖 State。

独立 CLI 从配置生成类型化壳。定向测试
`orpheus_core/tests/test_one_pole_envelope_interfaces.py`
用独立逐步 f32 参考验证 128 通道、冻结、负 alpha、同 Task 两次到达、
三出口顺序及 dynamic/generated × global/Task 四入口。
