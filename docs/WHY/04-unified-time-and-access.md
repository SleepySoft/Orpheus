---
title: 为什么统一时间与访问模型
type: WHY
order: 4
up: '[[00-index]]'
tags: [orpheus/why]
---

# 为什么统一时间与访问模型

实时设备、离线文件、外部节拍和嵌入式宿主会以不同节奏触发图。如果这些差异散落在组件里，同一算法会在不同宿主下表现不一致。

统一模型把以下概念分离：

- 执行实现：动态 Runtime 或生成代码。
- 执行触发：外部节拍或主动推进。
- 访问端点：本机、串口或其他 Bridge。
- 图时间线：块时间、绝对帧、epoch 和时钟域。

详细定义见 [[../HOW/reference/execution-model|执行模型]] 与 [[../HOW/reference/timeline|时间线设计]]。

