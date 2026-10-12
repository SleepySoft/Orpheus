---
title: 外部参考模型对齐
type: HOW
order: 13
up: '[[00-index]]'
tags: [orpheus/how]
---

# 外部参考模型对齐

部分 DSP 组件来自外部参考工程的结构化蒸馏。仓库只保留可实现的组件、参数和测试，不把外部源码或大表作为依赖。

## 原则

- 只记录可复现的组件映射。
- 外部路径只出现在本地验证过程，不进入工程配置。
- 数值正确性用 golden 或一致性测试验证。
- 双 Runtime 模型必须先更新模块索引；六项门禁未完成时只能发布独立切片，不能直接组装完整图。
- 物理传输包不直接等同于算法连线。混装音频、控制数组、状态和事件的 typed bus 必须按字段语义拆分展示，同时保留原始 offset、总长度和事务边界。
- 音频块 shape 必须写成 `[channels, frames]`，禁止把 `channels × frames` 展平成伪通道数。
- 研究图中的 Runtime 间语义链路是只读投影，不进入 `graph.connections`；只有调度、延迟、原子性和数据面实现完成验证后，才可 lowering 为可执行音频桥或控制桥。

详细记录见 [[reference/external-models/model-alignment|模型对齐]]。

双 Runtime 模型专项：

- [全量 Algorithm IR](../../examples/recycled/external_model_structural_reference/algorithm-index.yaml)
- [逐模块验证进度](../../examples/recycled/external_model_structural_reference/distillation-progress.yaml)
- [逐模块蒸馏流程](../../examples/recycled/external_model_structural_reference/distillation-workflow.md)
- [Big6 分析记录](../../examples/recycled/external_model_structural_reference/analysis/big6.md)
- [完整图蒸馏与投影记录](../../examples/external_model_full/analysis/distillation-process.md)
- [完整图交接 TODO](../../examples/external_model_full/TODO.md)
