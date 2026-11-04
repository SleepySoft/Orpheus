---
title: Runtime 与调度
type: HOW
order: 5
up: '[[00-index]]'
tags: [orpheus/how]
---

# Runtime 与调度

## 执行模型

- 动态 Runtime 加载组件 DLL 并按 Plan 执行。
- 生成代码按相同 Plan 静态展开。
- 图按节点速率域和 Task 划分执行入口。
- Task 只启动直连入口，组件通过 ABI v6 续接接口同步调用其直接下游。
- 分频在调用连接上计数；不设置会合屏障、不按拍去重、不扫描节点周期表执行。
- 缓冲水位决定数据是否可用，不触发下游 Task；消费链由自己的关联 Task 驱动。

## 时钟与速率

- 每个节点属于一个速率域。
- 跨速率合流使用显式 rate bridge。
- 跨 Task 音频必须经过异步桥。

详细设计见[组件链式调用](reference/task-driven-execution-design.md)和
[[reference/timeline|时间线设计]]。
