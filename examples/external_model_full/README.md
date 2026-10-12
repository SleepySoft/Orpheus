# Algorithm IR 全量研究图

本工程由 `algorithm-index.yaml` 确定性生成，共 66 个递归算法定义、11 个任务和 17 个参数分区。

双击 Runtime 和子系统可递归查看音频拓扑。递归子系统内部尚未 lowering 的 control/message/spectrum/state 边不伪装成普通音频线，完整记录保存在 `project.yaml` 的 `algorithm_projection` 与原始 IR 中。

顶层主链是 `2ch 节目输入 -> 30ch 系统输入装配 -> Runtime A -> 22ch 主输出`。A/B 的 typed bus 按字段类型拆成 Runtime 间音频与控制/状态只读投影，用于解释算法因果，但不会写入 graph.connections。

交接材料见 [`analysis/distillation-process.md`](analysis/distillation-process.md) 与 [`TODO.md`](TODO.md)；重新生成工程时这两个手工维护文件会保留。

叶节点为 placeholder 时表示算法尚未完成 golden 或运行时能力仍缺失，编译器会主动拒绝；这张图用于完整分析和逐模块替换，不得宣称可执行或数值等价。
