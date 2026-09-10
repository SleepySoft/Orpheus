---
title: 代码生成与平台适配
type: HOW
order: 7
up: '[[00-index]]'
tags: [orpheus/how]
---

# 代码生成与平台适配

## 目标

`win` 生成可直连声卡的 PC 工程；`dsp` 生成嵌入式骨架。

## 结构

生成工程包含：

- 静态图实现。
- 最小宿主或平台适配层。
- 组件源码与 CMake。
- 控制链路 tick。

## 平台解析

编译器根据组件平台声明和图内 alter 关系确定可达平台，选择激活成员，并重映射连接。

PC 生成的目标形态见 [[reference/pc-generated-app|PC 配置好即完整程序]]。
