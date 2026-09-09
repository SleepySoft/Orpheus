---
title: 测试与验证
type: HOW
order: 10
up: '[[00-index]]'
tags: [orpheus/how]
---

# 测试与验证

## 后端

`python -m pytest orpheus_core/tests/` 覆盖编译、平台解析、子组件、时钟、控制链路、Bridge 和一致性。

## 前端

`cd ui; npm test -- --watchAll=false` 覆盖纯函数和图工具。

## 端到端

`cd ui; npm run test:e2e` 验证核心编辑流程。

## 红线

- 实时路径禁止动态分配、阻塞、文件和网络 IO。
- 组件入口必须用统一宏导出。
- 源码保持 UTF-8 无 BOM。

