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

### 日常改动使用定向验证

- 先选择直接覆盖改动的测试文件或用例，合并相关选择器执行一次；不要每个小改动重复跨模块回归。
- 组件语义变化验证独立数值参考及相关执行入口；仅两条路径输出一致，不能代替算法正确性。
- 编译/调度变化验证对应的编译拒绝、运行入口及状态边界；不因修改了共享文件就立即重复整个模型的验收。
- 复用已通过且依赖未变的结果。受影响但尚未重验的来源证明明确保留为待复核，不能只刷新哈希冒充通过。
- 全量回归集中在集成里程碑执行；仅在定向验证暴露跨域问题时提前扩大范围。

例如整数BULK改动先运行
`python -m pytest orpheus_core/tests/test_integer_bulk_delay.py -q`，
仅修改Bridge编解码时选择
`python -m pytest orpheus_core/tests/test_serial_session.py -q -k bulk`。

### 小单元提交

每个可独立验收的改动通过对应验证后立即提交，再开始下一项。
提交只包含本次改动，不混入用户已有的工作树修改。文档单独修改无需重复运行代码测试。

## 前端

`cd ui; npm test -- --watchAll=false` 覆盖纯函数和图工具。

## 端到端

`cd ui; npm run test:e2e` 验证核心编辑流程。

## 红线

- 实时路径禁止动态分配、阻塞、文件和网络 IO。
- 组件入口必须用统一宏导出。
- 源码保持 UTF-8 无 BOM。
