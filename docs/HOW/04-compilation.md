---
title: 工程编译与执行计划
type: HOW
order: 4
up: '[[00-index]]'
tags: [orpheus/how]
---

# 工程编译与执行计划

编译器把 YAML 工程转换成 Plan。

## 主要步骤

1. 读取 registry 与 manifest。
2. 展开 subcomponents。
3. 解析 alter 和平台可达性。
4. 校验端口、类型、速率、Task 和控制链路。
5. 分配 buffer 与生成调度。
6. 产出 topology、schedule、tasks、control_links 和元数据。

Plan 是动态 Runtime 和代码生成路径共享的事实来源。

## 非音频计算数据

非音频端口的数组尺寸与处理节拍分开推导：同步数据依赖参与交付周期约束，
`timing: latest`输入只读取同Task最新结果，不驱动节点执行。默认同步，禁止隐式
广播和跨Task直连。现阶段支持F32标量/向量/矩阵数据，非F32格式显式报错。

这与`control_connections`的参数快照更新不同。完整契约与数值验收见
[时钟调度：非音频计算数据](reference/clock-scheduling.md#非音频计算数据2026-11)。
