---
name: orpheus
description: Orpheus 音频图引擎的开发指南：编写/修改 C 组件（C ABI、manifest、可变引脚、UI 控件元数据）、外部 C/C++ 模型蒸馏与 Algorithm IR、图编译、Runtime、后端 API、UI 联动、实时音频宿主和代码生成。当任务涉及 Orpheus 组件开发、完整算法蒸馏、目录工程、执行/生成双路径或音频与控制链路建模时使用。
---

# Orpheus 开发指南

可视化音频处理框架：YAML 工程图 → 编译为执行计划 → C ABI 组件（DLL 动态加载或静态代码生成）。技术栈：C11/C++11（CMake+Ninja+MinGW GCC）、Python 3.12（FastAPI 后端）、React（CRA + React Flow UI）。

## 目录结构速查

| 路径 | 内容 |
|---|---|
| `orpheus_abi/include/orpheus_abi.h` | C ABI 契约（组件与 Runtime 之间唯一接口） |
| `components/orpheus/builtin/<name>/` | 组件：`component.yaml` + `src/*.c` + `include/*.h` + `CMakeLists.txt` |
| `orpheus_core/orpheus_core/` | Python：registry / compiler / builder / generator / subgraph / server / cli |
| `orpheus_runtime/` | C++：runtime 引擎、`orpheus_runtime.exe`（文件宿主）、`orpheus_rt_host.exe`（设备宿主） |
| `ui/src/` | React 前端（App.js 主控、widgets.js 控件注册表、nodeWidgets.js 节点本体注册表） |
| `examples/*.yaml` | 示例工程；`workspace/<name>/project.yaml` 用户工程（gitignored） |

## 常用命令

```powershell
python serve.py                    # 启动后端+UI（http://127.0.0.1:8000），PyCharm 可直接 Debug
python -m orpheus_core.cli build   # 构建全部组件 + runtime（cmake -G Ninja）
python -m pytest orpheus_core/tests/   # 后端全量测试（数量以 CI 输出为准）
cd ui; npm run build               # 前端改动后必须重新构建，serve 才托管新版本
cd ui; npm run test:e2e            # Playwright：真实后端 + 浏览器核心流程
```

环境注意（2026-08-06）：

- Python 必须 ≥3.10（`dev` conda 环境是 3.8，不可用；用 `py310` 或 `base`）。`orpheus_core` 需在目标环境 `pip install -e "orpheus_core[dev]"`。
- CMake/Ninja 由 VS 2022 自带（`Common7\IDE\CommonExtensions\Microsoft\CMake\{CMake\bin,Ninja}`），已加入用户 PATH；`cli build` 检测到 MSVC 后自动加载 vcvars64，普通终端可直接构建。
- `cli build`（无参数）会顺带重建 `orpheus_runtime`/`orpheus_rt_host`；组件与 runtime 必须同代重建，否则 ABI 不匹配（新组件读旧 runtime 的 `OrpheusConfig.state_block` 属越界读，会导致 balance 等组件行为异常）。

## 红线（违反必出 bug，都是踩过的坑）

1. **实时路径禁令**：组件 `process` 中禁止 malloc/free、阻塞锁、文件/网络 IO、printf、异常。日志只在 create/prepare/destroy/set_parameter 里输出。
2. **编码**：源码一律 UTF-8 无 BOM + LF。禁止用 PowerShell `Get-Content/Set-Content` 改源码（默认 GBK 会毁中文）。子进程管道必须 `encoding="utf-8"`。
3. **工具链**：PATH 里 Perl 自带的 gcc 4.9.2 不可用；用 Strawberry GCC（主构建 `build/CMakeCache.txt` 的 `CMAKE_C_COMPILER` 为准，生成工程 configure 要显式传）。
4. **组件入口符号**：组件 .c 的入口必须是 `ORPHEUS_ENTRY_NAME` 宏包裹的函数（缺省 `orpheus_get_interface`），否则静态链接时多组件符号冲突崩溃。新组件照抄现有组件写法。
5. **Buffer 绑定按端口 ID**：多端口组件的 `ctx->inputs[]/outputs[]` 槽位对应 manifest 端口声明顺序（可变端口展开后）；未连接引脚为 NULL，process 必须判空。
6. **设备回调周期 ≠ 图块长**：回调里必须按 block_size 分块处理（参考 rt_host.cpp），周期请求下限 10ms。
7. **ABI v2 状态结构体公开**：新组件（或迁移组件）状态结构体必须放进 `include/` 头文件并在 manifest 声明 `state_type`（生成器按类型拼接 `g_arena`）；`create` 使用 `config->state_block` 下发的内存块，`destroy` 不再 `free`。
8. **可寻址槽用一行宏注册**：`ORPHEUS_REG_SLOT` / `ORPHEUS_REG_ARRAY` 注册 SETTING/PROBE/BULK 等槽（key 与 manifest 参数 id 对齐）；标量槽由 Runtime 直读直写，数组槽回退 `get_parameter` 回调。详见 `references/abi-v2-registration.md`。
9. **生成路径三件事**：MSVC 生成工程必须 `/utf-8`（中文 STRING 字面量否则编译错）；float/int 参数按 manifest 类型下发（字符串会被 prepare 忽略）；节点 id 会清洗为合法 C 标识符（含 `.`），用户命名别用特殊字符。

## ABI v2：资源槽注册（2026-08-06 落地）

统一内存拼接分配：动态路径按 `descriptor.state_size` 切片下发，生成路径按 `state_type` 类型拼接 `g_arena`；组件在 `register_slots` 里用一行宏把"地址/类型/说明"注册给 Runtime，Runtime 建槽表并做边界校验。

- 设计全文：`docs/HOW/reference/registry.md`（槽模型、32 位 ID、边界检测、聚合布局、实证修正）。
- 现有内置组件均遵循公开状态/arena 契约；资源槽按组件需要注册，旧回调仅作为兼容兜底。
- 写新组件：见 `references/abi-v2-registration.md` 的检查清单。

## 模型蒸馏：证据索引 → 逐模块实现 → 完整系统图

当任务涉及外部/公司 C/C++ 模型、生成代码、TOP/RTC 参数、多个 Runtime/TID 或完整算法蒸馏时，**必须先读** `references/distill-model.md`。以下规则不可省略：

### 完成度分层

- **结构复刻不等于算法实现**：完整层级、端口和连线可以先进入研究图，但未实现叶节点必须是 `package_type: placeholder`，并由编译器硬阻断。
- `composite` 只表达组合与边界；`placeholder` 是空实现/待蒸馏边界；只有 source/binary 或完全由可执行通用组件组成的 composite 才能称为实现。
- UI 可见、schema 通过、能生成 plan 都不能替代公式、状态、参数和 golden 验证；禁止把 approximation 或空节点称为“蒸馏完成”。

### 证据优先流程

1. 先固定源文件 SHA-256，并建立严格 Algorithm IR：模块层级、类型、真实 `[channels, frames]`、Task/TID、状态、参数分区、关系、bus 字段/offset 和 unresolved。
2. 每个模块分别维护 `boundary / schedule / algorithm / state / parameters / golden` 六门禁；任一未验证，只能是研究图或独立切片。
3. 先分析调用者与被调用者、生产者与消费者，再决定端口；禁止只看函数名或单个函数签名直接画图。
4. 逐模块形成分析记录、实现、参数提取、golden 和独立可运行切片；门禁通过后才替换完整图中的 placeholder。
5. 用户要求“完整/全量蒸馏”时，全部 placeholder 就是必须清零的工作队列；除非缺失外部证据形成真实 blocker，否则不得在索引、计划或局部实现阶段停止。每完成一项立即更新进度与 TODO。

### 实现选择顺序

1. **已有通用组件直接复用**：先搜索 builtin、相邻示例和组件 README，确认语义、状态和数值边界都匹配。
2. **通用组件组合**：能由 FIR/IIR/matrix/mixer/delay/rate/bridge 等组合表达时，做 composite，不写专用 C。
3. **新增通用组件**：现有组件不够但算法可跨项目复用时，设计通用参数/端口/状态 API，放入 `components/orpheus/builtin/`，补 README、数值测试和动态/生成一致性测试。
4. **项目独有组件**：只有算法确实无法合理泛化，且源码证据、状态机、参数布局和 golden 均完整时才允许；必须记录“为什么不能通用化”。生成目录会被覆盖，手写源码必须放入非生成组件包路径，并由生成器写入 imports，不得直接塞进生成器拥有的 `components/`。

### 图语义红线

- 画布只允许框架已有的**标准音频链路**和**标准控制链路**，不得发明第三种 edge/handle/label 冒充算法关系。
- 跨 Runtime 事务中，音频字段必须导出为标准 audio ports 并写入 `graph.connections`；控制/状态字段只有具备 `control_source/bindable/shape/maps_to` 后才能写入 `control_connections`，否则留在 Algorithm IR。
- 物理打包长度（words/bytes）不是音频通道数；音频块必须保持 `[channels, frames]`。混合事务的原子性、sequence 和延迟单独记录，不能用伪处理组件或无行为回环表示。
- 未连接端口、跨 Task/跨速率音频、数组控制和事务桥必须遵守 Compiler/Runtime 现有契约；研究图不能绕过系统校验。

### 交付与同步

- 完整目录工程同时维护：权威索引、确定性生成器、`analysis/` 蒸馏过程、`TODO.md`、节点笔记和覆盖报告。
- 生成文件与手工文件边界必须写清；修改算法事实时，同一提交同步代码、索引、测试、原则文档、分析记录和 TODO。
- 小型单速率模型仍可使用 `model_tree` / 一键导入兼容路径；复杂多 Runtime 模型禁止从生成 C 代码直接转图。

## 任务索引（按需加载 references/）

| 任务 | 读这个 |
|---|---|
| 写 v2 组件（公开状态/注册槽/拼接内存） | `references/abi-v2-registration.md` |
| 写新组件 / 改组件 manifest / 可变引脚 / UI 控件定制 | `references/write-component.md` |
| 理解架构：两种执行模式、ABI、plan、宿主分工、API 面 | `references/architecture.md` |
| 运行/调试：实时会话协议、日志约定、故障排查目录 | `references/run-debug.md` |
| 工程 YAML 格式、子组件（sub:）、workspace 布局 | `references/project-format.md` |
| 模型蒸馏：证据索引、六门禁、组件决策树、目录研究图、逐模块实现与交接 | `references/distill-model.md` |

> 当前状态与待办以 `docs/HOW/roadmap.md` 为准；实现历史见 `docs/HOW/implementation-log.md`。

## 最小工作流

- **加组件**：复制最相近的现有组件目录 → 改 id/manifest/实现 → `cli build` → 写一个跑通的示例工程验证（参考 `references/write-component.md` 的检查清单）。
- **改 UI**：`ui/src/` 编辑 → `npm run build` → serve 托管即更新。参数控件走 `widgets.js` 注册表；节点本体定制走 `nodeWidgets.js`。
- **改编译/运行逻辑**：Python 侧改完跑 `pytest orpheus_core/tests/`；C++ 侧改完 `cmake --build build --target orpheus_runtime orpheus_rt_host`（实际分两条 target 命令）并跑相关 e2e 测试。

## 文档与笔记体系（2026-08-10）

Orpheus 把“可读性”视为核心能力。仓库里存在三类互补的文档，分别面向不同学习场景：

| 类型 | 文件位置 | 读者 | 内容要求 |
|---|---|---|---|
| **组件 README** | `components/orpheus/builtin/<name>/README.md` | 任何想用这个组件的人 | 这个组件到底做什么、参数怎么理解、常见坑、典型用法 |
| **工程笔记** | `workspace/<project>/notes.md` | 读/维护这个工程的人 | 工程整体意图、信号流解释、调参记录、设计取舍 |
| **节点笔记** | `workspace/<project>/node-notes.json` | 查看具体节点的人 | 这个节点在当前工程里为什么存在、参数为什么这么设 |

### 组件 README 写法

每个 `README.md` 都会被 UI 的节点 `ℹ` 按钮渲染。好的组件 README 不是把 `component.yaml` 照抄一遍，而是回答以下问题：

1. **这个组件解决什么问题？** 用一句话+一段例子说明。例如 `delay_line` 不是“一个延迟线”，而是“按通道独立延迟，用于扬声器时间对齐、人工混响预延迟或相位微调”。
2. **参数的真实含义是什么？** 单位、范围、更新策略、常见取值。例如 `gain_db` 是 dB，但 0 dB 是直通，-96 dB 近似静音；`smoothed` 更新会在 `smoothing_time_ms` 内渐变。
3. **端口签名如何变化？** 哪些参数影响引脚数量（如 `channels`、`count`），改变后需要重新编译。
4. **有什么坑？** 实时路径限制、NULL 输入必须判空、延迟/块长/采样率的隐含要求。
5. **典型连接方式？** 前面通常接什么、后面通常接什么，举一个小图片段。

模板参考（以 `channel_router` 为例）：

```markdown
# orpheus.builtin.<name> — 中文显示名

## 功能
一句话说明。再补充一段场景。

## 参数
| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| ... | ... | ... | ... |

## 端口
- `in`: 输入，N 通道音频。
- `out`: 输出，M 通道音频（由 `channels_out` 决定）。

## 注意事项
- ...

## 典型用法
```
...（小图或连接说明）
```
```

### 工程笔记与节点笔记

- **工程笔记 `notes.md`**：左侧“笔记”标签页可编辑/预览。用来自由书写：整体信号流、为什么这样分区、每个子系统的职责、调参记录、待办等。
- **节点笔记 `node-notes.json`**：选中节点后在参数面板编辑，自动按 `node_id` 切片保存。节点头部出现 📝 徽标。左侧“笔记”面板底部可汇总查看/编辑所有节点笔记。
- 两者都**不写入 `project.yaml`**，保持工程文件 diff 友好、编译无关。

### 给 示例工程/复杂工程写文档的原则

- **从“为什么”开始**：不要只写“这是一个 IIR 滤波器”，要写“这里用 13 级 poolIIR 做外周 EQ，因为 示例工程 需要按通道独立补偿车厢响应”。
- **按信号流组织**：工程笔记按 `音乐输入 → 预处理 → FDP → 混音 → EQ → 输出` 等章节展开。
- **参数讲究单独成段**：哪些参数来自 TOP 文件、哪些是经验默认值、调节时听感如何变化。
- **节点笔记聚焦实例**：只解释该节点在当前工程中的作用；通用组件行为留给组件 README。
