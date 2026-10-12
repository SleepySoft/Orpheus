# Algorithm IR 全量研究图

本工程由 `algorithm-index.yaml` 确定性生成，共 66 个递归算法定义、11 个任务和 17 个参数分区。

双击 Runtime 和子系统可递归查看音频拓扑。control/message/spectrum/state 边无法投影为普通音频线，完整记录保存在 `project.yaml` 的 `algorithm_projection` 与原始 IR 中。

顶层三条实线是 `2ch 节目输入 -> 30ch 系统输入装配 -> Runtime A -> 22ch 主输出`。A/B 之间的两条青色虚线由 typed bus 合同只读投影，不属于音频处理，也不会写入 graph.connections。

叶节点为 placeholder 时表示算法尚未完成 golden 或运行时能力仍缺失，编译器会主动拒绝；这张图用于完整分析和逐模块替换，不得宣称可执行或数值等价。
