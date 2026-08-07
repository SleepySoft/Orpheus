---
name: orpheus
description: Orpheus 音频图引擎的开发指南：编写/修改 C 组件（C ABI、manifest、可变引脚、UI 控件元数据）、使用框架（编译/运行/实时会话/代码生成）、排查构建与运行问题。当任务涉及 Orpheus 仓库的组件开发、图编译、Runtime、后端 API、UI 联动、实时音频宿主，或需要在 Orpheus 中新增/调试音频处理组件时使用。
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
python -m pytest orpheus_core/tests/   # 全部测试（当前 44 项）
cd ui; npm run build               # 前端改动后必须重新构建，serve 才托管新版本
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

- 设计全文：`docs/design_registry.md`（槽模型、64 位 ID、边界检测、聚合布局、实证修正）。
- 试点组件：`gain` / `probe_rms` / `probe_waveform`（其余组件仍是 v1 回调路径）。
- 写新组件：见 `references/abi-v2-registration.md` 的检查清单。

## 模型蒸馏：分析 C 代码 → 还原滤波器树 → 一键导入

当任务涉及「分析公司/目标模型的 C/C++ 代码，反向还原其滤波器构造与参数，输出树形说明并在 Orpheus 一键导入」时，先读 `references/distill-model.md`：

- 产物 1：可读树形说明（顶层 `model_tree`，标注滤波器类型与每个参数）+ Markdown 分析说明。
- 产物 2：可直接导入的工程 YAML（`graph` + 嵌套 `subcomponents`，三层嵌套示例见 `examples/dsp_model_reference.yaml`）。
- 一键导入：UI 工具栏「⤵ 导入模型」，或 `POST /api/projects/{name}/distill`（body `{"yaml": "..."}`）。
- 验证：`python scripts/parameter_layout.py <project.yaml>` 打印数据 layout 并回写校验；`cli compile` 通过；可跑的图再跑一次 e2e。

## 任务索引（按需加载 references/）

| 任务 | 读这个 |
|---|---|
| 写 v2 组件（公开状态/注册槽/拼接内存） | `references/abi-v2-registration.md` |
| 写新组件 / 改组件 manifest / 可变引脚 / UI 控件定制 | `references/write-component.md` |
| 理解架构：两种执行模式、ABI、plan、宿主分工、API 面 | `references/architecture.md` |
| 运行/调试：实时会话协议、日志约定、故障排查目录 | `references/run-debug.md` |
| 工程 YAML 格式、子组件（sub:）、workspace 布局 | `references/project-format.md` |
| 模型蒸馏：分析 C 代码 → 还原滤波器树与参数 → 生成可导入工程 | `references/distill-model.md` |

> 注：`write-component` / `architecture` / `run-debug` / `project-format` 四个 reference 尚未落盘，当前以仓库实际代码与 `docs/` 为准。

## 最小工作流

- **加组件**：复制最相近的现有组件目录 → 改 id/manifest/实现 → `cli build` → 写一个跑通的示例工程验证（参考 `references/write-component.md` 的检查清单）。
- **改 UI**：`ui/src/` 编辑 → `npm run build` → serve 托管即更新。参数控件走 `widgets.js` 注册表；节点本体定制走 `nodeWidgets.js`。
- **改编译/运行逻辑**：Python 侧改完跑 `pytest orpheus_core/tests/`；C++ 侧改完 `cmake --build build --target orpheus_runtime orpheus_rt_host`（实际分两条 target 命令）并跑相关 e2e 测试。
