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

