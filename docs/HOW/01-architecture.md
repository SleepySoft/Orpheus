---
title: 架构与技术栈
type: HOW
order: 1
up: '[[00-index]]'
tags: [orpheus/how]
---

# 架构与技术栈

## 分层

```text
UI: React + React Flow
Service: FastAPI + uvicorn
Compiler: YAML -> Plan
Runtime: C++ 执行引擎
Components: C/C++ + C ABI
Bridge: 本机 / 串口 / HLOS Transport
```

## 关键决策

- UI 与后端通过 REST 交互，不把编译逻辑塞进前端。
- 组件对外只暴露稳定 C ABI。
- Plan 是编辑器和运行系统的边界。
- 生成路径不依赖 Python 和 UI。

历史详细说明见 [[reference/how-legacy|原始 HOW 全文]]。

