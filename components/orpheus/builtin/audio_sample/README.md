# 音频采样点导出

`component.yaml` 的 `interfaces` 是声明事实源，独立组件脚本生成
`generated/`，算法位于 `user/algorithm.c`。

输入到达后，取配置 `frame` 处各通道的原始浮点位型，依次填充
标量 `out0..outN-1`，每填完一路就同步发射其下游。
出口编号为通道索引加 1，不意味着框架另行调度这些出口。
NULL 输出跳过，零处理量子不写数据；先检查全部输出别名再开始交付。

纯 `validate` 钩子拒绝 `frame >= frames` 的候选配置。
标量 BULK 的一项载荷不改变 Task 量子，也不隐式执行整数转换；
需要整数时显式连接 `scalar_to_i32`。
