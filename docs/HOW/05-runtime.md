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

## 时钟与速率

- 每个节点属于一个速率域。
- 跨速率合流使用显式 rate bridge。
- 跨 Task 音频必须经过异步桥。

详细设计见 [[reference/clock-scheduling|时钟调度]] 和 [[reference/timeline|时间线设计]]。

