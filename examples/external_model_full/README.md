# Algorithm IR 全量研究图

本工程由 `algorithm-index.yaml` 确定性生成，共 68 个递归算法定义、11 个任务和 17 个参数分区。

双击 Runtime 和子系统可递归查看音频拓扑。control/message/spectrum/state 边无法投影为普通音频线，完整记录保存在 `project.yaml` 的 `algorithm_projection` 与原始 IR 中。

叶节点为 placeholder 时表示算法尚未完成 golden 或运行时能力仍缺失，编译器会主动拒绝；这张图用于完整分析和逐模块替换，不得宣称可执行或数值等价。
