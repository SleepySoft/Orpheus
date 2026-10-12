# 完整双 Runtime 模型：分析与蒸馏记录

> 更新日期：2026-10-12
>
> 本文记录 `external_model_full` 从外部生成代码到 Algorithm IR、再到 Orpheus 研究图的决策过程。它是交接记录，不是数值等价声明。

## 1. 目标与边界

目标不是复刻外部函数名或画一张外形相似的图，而是恢复以下语义：

- Runtime A / B 的模块边界与调用方向；
- 11 个 Task 的速率和调用顺序；
- 音频、控制、状态、事件和频域数据的真实 shape；
- 跨 Runtime 事务的字段布局、生产者、消费者和 offset；
- 每个叶模块距离可执行、可验证实现还缺什么。

当前工程是完整的**研究图**。叶节点仍含 placeholder，编译器会主动拒绝，不得宣称已可运行或与参考实现数值等价。

## 2. 权威资料

| 资料 | 角色 |
|---|---|
| `algorithm-index.yaml` | 结构权威：66 个递归定义、11 个 Task、类型、关系、双向 bus 和未决事实 |
| `coverage-report.json` | 生成覆盖率与 blocker 汇总 |
| `project.yaml` | 从 Algorithm IR 确定性生成的 Orpheus 研究图 |
| `node-notes.json` | 画布节点用途与边界说明 |
| `../recycled/external_model_structural_reference/distillation-progress.yaml` | 25 个模块的六门禁进度账本 |
| `../recycled/external_model_structural_reference/distillation-workflow.md` | 单模块蒸馏流程与发布门禁 |
| `../recycled/external_model_structural_reference/analysis/` | Big6 等专题源码分析 |

外部源码只用于校验索引中的 SHA-256 和补充证据，不进入工程依赖。

## 3. 蒸馏过程与关键决策

### 3.1 先建索引，再生成图

最初按函数/模块名称直接拼图会遗漏并行支路、跨 TID 状态、数组控制和事务顺序。因此先建立严格 Algorithm IR：

1. 固定源文件及 SHA-256；
2. 建立稳定模块 ID、父子层级和接口；
3. 记录 Task、周期和 ordinal call；
4. 记录类型、状态、参数分区和跨模块关系；
5. 把尚未证实的事实放入 `unresolved`；
6. 最后确定性生成组件 manifest 和顶层工程。

`distillation-progress.yaml` 仍是六门禁进度账本，不能用“已进入完整图”替代 golden、状态或参数验证。

### 3.2 删除伪处理组件

早期图用 `BUS_EXCHANGE` 节点和回环表示 A/B 通讯，但这些节点不执行算法，容易被误解为音频处理。它们已删除。跨 Runtime 关系保留为 typed bus 合同，其中音频字段通过 Runtime 根组件的标准端口连接。

### 3.3 补齐系统 I/O

顶层主音频链明确为：

```text
2ch program_input
  -> 2→30 program_to_system
  -> Runtime A audio_in
  -> Runtime A main_out
  -> 22ch main_output
```

`program_to_system` 是测试/部署适配，不属于目标算法。Runtime B 通过跨 Runtime 数据参与计算，不伪造一条普通串联音频边。

### 3.4 物理事务与算法语义分层

一个物理 bus 可以同时装入音频块、控制数组、状态和事件。直接画成一条“消息线”虽然保留包边界，却无法解释算法因果；直接画成音频线又会把增益和 sequence 误当采样数据。

当前采用两层表示：

- **事务层**：`algorithm_projection.buses` 保留总 words、offset、字段生产者/消费者和 atomicity；
- **执行图层**：音频字段导出为 Runtime 根组件的标准 audio ports，并写入 `graph.connections`；控制/状态字段只有在建立标准公开参数映射后才可写入 `control_connections`。

当前控制/状态字段不画线，避免产生 audio/control 之外的第三种连接。物理事务的原子性与延迟尚未验证，因此标准音频边只表达已确认的音频生产者、消费者和端口格式，不代表跨 Runtime 数据面已经实现。

### 3.5 音频 shape 不展平

音频块必须写成 `[channels, frames]`：

- Mono：`[1, 32]`
- Treble LR：`[2, 32]`
- Treble Surround：`[7, 32]`
- Adaptive Audio：`[10, 32]`
- Post Spatial：`[22, 32]`

旧的 `[32] / [64] / [224] / [320] / [704]` 只表达 word 数，会让 manifest 把帧数误判为通道数。word 数仍由 shape 各维乘积得到。

## 4. 当前 Runtime 间字段与标准连接

| 方向 | 类别 | 字段 | 画布表示 |
|---|---|---|---|
| A→B | 音频 | `adaptive_audio [10×32]` | 标准 10ch audio connection |
| A→B | 控制/状态 | `center_current [26]`、`left_current [70]`、`right_current [70]`、`fade_targets [2]` | 暂不画线，保留 IR |
| B→A | 音频 | `mono [1×32]`、`treble_lr [2×32]`、`treble_surround [7×32]`、`post_spatial [22×32]` | 四条标准 audio connections |
| B→A | 控制/状态 | enable、decay、三组 target gains、fast sequence | 暂不画线，保留 IR |

因此：

$$
320+168=488
$$

$$
1024+169=1193
$$

数量不同不是协议错误。Runtime B 返回了额外的 `22×32=704` 空间处理结果，所以 B→A 明显更大。

## 5. 生成与手工文件边界

以下文件由生成器覆盖，不手工编辑：

- `project.yaml`
- `algorithm-index.yaml`
- `components/*.component.yaml`
- `node-notes.json`
- `coverage-report.json`
- `README.md`

以下文件手工维护，重新生成时保留：

- `analysis/distillation-process.md`
- `TODO.md`

生成源：

- `scripts/build_external_full_index.py`
- `orpheus_core/orpheus_core/algorithm_index.py`

重生成命令：

```powershell
python scripts/build_external_full_index.py examples/recycled/external_model_structural_reference/algorithm-index.yaml
python -m orpheus_core.cli index-generate examples/recycled/external_model_structural_reference/algorithm-index.yaml examples/external_model_full
```

## 6. 当前验证

- Algorithm IR schema、重复 YAML key、引用闭包和 bus offset 全覆盖测试；
- 索引构建器与提交版 `algorithm-index.yaml` 字节级一致；
- 完整工程与临时生成目录字节级一致；
- placeholder 编译阻断测试；
- UI 纯函数测试确认 typed bus 元数据不生成第三种边，标准 audio/control 连接正常往返；
- 浏览器确认 A/B 之间仅显示 5 条标准音频边，无投影 edge/handle/label；
- 31/31 示例结构校验通过，完整研究图按预期报告 placeholder warning。

## 7. 交接约束

- 不要重新引入无算法行为的 bus 处理节点或 A/B 音频回环；
- 不要把 bus 总 words 当通道数；
- 不要把 bulk 数组降格为标量控制链，也不要在标准控制映射完成前画线；
- 不要因 UI 能显示完整图就宣称可执行或数值等价；
- 修改字段、shape、Task 或模块所有权时，同一提交更新索引、构建脚本、测试、本文和 `TODO.md`。
