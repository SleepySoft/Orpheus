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

详细记录见 [[reference/external-models/model-alignment|模型对齐]]。
