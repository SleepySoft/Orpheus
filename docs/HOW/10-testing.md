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

### Task 顺序调用框架

`python -m pytest orpheus_core\tests\test_task_flow.py -q` 验证显式驱动与数据绑定分离、
Source/Task 归属、周期/相位、无音频计算、复合展开、保存/API 往返及错误拒绝。
其中原生 runner 用动态/生成、全局/Task 入口核对：

- 同次标量参数已经作用到实际音频输出，独立 PCM 参考不只比较两路同错；
- typed BULK 在目标调用前生效，`every=2` 的两种相位分别验证跳过/保持；
- 无关 Task 入口不能提交其它 Task 的外部 BULK 待写；
- 旧 `test_control_links.py`、`test_static_schedule.py` 保留旧模式回归。

运行数值用例前，定向重建 `task_flow_runner`、Runtime 与相关组件；
前端驱动边的保存/复制/重连及节拍字段由 `graphUtils.test.js` 覆盖。
详细支持范围见[Task 迁移设计的实现状态](reference/task-driven-execution-design.md#0-首批实现显式调用静态节拍与调用前参数交付)。

### 模型蒸馏的验收与断点记录

- 数值正确性与执行一致性分开验证：先以原代码或独立参考核对算法，再比较动态全局、
  动态Task、生成全局、生成Task四入口。仅四入口相等，不能证明原算法已还原。
- 状态组件按实际契约覆盖首次调用、非零跨块、参数更新、停用/恢复及reset。
  停用冻结不能以末端干湿选择代替；输出后初始化不能提前成输出前初始化。
- 逐位/逐字节断言、输出长度和必要的非零/变化断言均须明确；宿主WAV量化比较若使用
  PCM容差，单独记录，不把它描述为与原浮点结果逐位相等。
- 工程内的`analysis/index.yaml`保留来源索引、TODO、依赖指纹和局部验收范围；
  `analysis/source_units.json`关联源单元，`notes.md`记录阶段统计、提交与继续入口。
  组件DLL需要持久构建，不能仅在临时测试工程生成后便宣称工程可直接调用。
- 占位数量、已实现职责切片和当前有效证明是不同指标。父图或依赖变更会使旧证明过期，
  不等于数值回归；保留待复核状态，不能修改指纹冒充重新执行。
- 参数入口、算法核、原RTC请求/响应事务和完整模型生命周期分别验收；
  局部TODO完成不代表完整模型通过。

近期状态组件的窄验收示例（Windows，研究工程需已存在于本机）：

```powershell
python -m pytest orpheus_core\tests\test_pool_delay.py -q
python -m pytest workspace\ExperimentalModel\baf_skeleton\tests\test_output_delay_leaf.py workspace\ExperimentalModel\baf_skeleton\tests\test_hf_noise_hold_leaf.py -q
```

阶段结果见[实施日志](implementation-log.md)，具体研究工程断点见其`notes.md`；
上述命令只验收相关叶子，不是全模型回归。

## 前端

`cd ui; npm test -- --watchAll=false` 覆盖纯函数和图工具。

## 端到端

`cd ui; npm run test:e2e` 验证核心编辑流程。

## 红线

- 实时路径禁止动态分配、阻塞、文件和网络 IO。
- 组件入口必须用统一宏导出。
- 源码保持 UTF-8 无 BOM。
