# AGENTS.md — Orpheus 仓库上下文

> 给在此仓库工作的 AI/人类协作者：先读本文，再按需深入 `docs/` 与 `SKILL/SKILL.md`。

## 项目是什么

**Orpheus** 是一个基于可视化编程的音频处理框架：用户在图形界面（React + React Flow）上拖放组件、连线、调参，工程以 YAML 描述；编译为执行计划（plan）后，由 C++ Runtime 动态加载 C ABI 组件（PC 实时/离线运行），或生成独立、可读、可编译的 C/C++ 工程（嵌入式部署路径）。

一句话定位：*"在电脑上像剪映一样直观地搭音频系统，生成的代码却像手工写的嵌入式模块一样干净可控。"*

中文优先：组件显示名（`name`）、提示与错误信息均为中文。

## 技术栈

| 层 | 选型 |
|---|---|
| UI | TypeScript 风格 React 18 + React Flow 11（CRA，`ui/`） |
| 后端/编排 | Python 3.10+，FastAPI + uvicorn（`orpheus_core/`） |
| 实时运行时 | C++11，CMake + Ninja + MinGW GCC（`orpheus_runtime/`） |
| 组件 | C11（入口经 `ORPHEUS_ENTRY_NAME` 宏导出 C ABI），允许 C++11 |
| 音频后端 | miniaudio（vendored，`third_party/`） |
| 工程格式 | YAML（`project.yaml`）+ JSON Schema 校验 |
| 测试 | pytest（Python）、C++ ABI smoke test、动态/生成双路径一致性测试 |

## 目录结构速查

| 路径 | 内容 |
|---|---|
| `orpheus_abi/include/orpheus_abi.h` | **C ABI 契约**：组件与 Runtime 之间唯一接口（descriptor / ports / parameters / process） |
| `components/orpheus/builtin/<name>/` | 组件：`component.yaml` + `src/*.c` + `include/*.h` + `CMakeLists.txt` |
| `orpheus_core/orpheus_core/` | Python：`registry` / `compiler` / `builder` / `generator` / `subgraph` / `server` / `cli` |
| `orpheus_core/tests/` | pytest 测试（server API、子组件展开、可变引脚、时钟/速率） |
| `orpheus_runtime/` | C++：`runtime.cpp`（执行引擎）、`rt_host.cpp`（实时设备宿主）、`main.cpp`（文件宿主）、`wav_io` |
| `ui/src/` | React 前端：`App.js` 主控、`widgets.js` 参数控件注册表、`nodeWidgets.js` 节点本体注册表、`graphUtils.js` |
| `examples/*.yaml` | 示例工程（可导入 UI） |
| `workspace/<name>/project.yaml` | 用户工程（唯一事实来源，已 gitignore） |
| `docs/` | `WHAT.md`（需求）、`HOW.md`（架构+实现记录）、`IMPLEMENTATION_PLAN.md`、`implementation_log.md` |
| `SKILL/SKILL.md` | 仓库专用开发技能：组件开发、调试、红线清单 |

## 核心架构要点（先理解再动手）

### 两条执行路径（共享同一份组件源码与 ABI）

1. **动态加载路径（UI「▶ 运行」）**：图编译只产出 plan.json（拓扑、Buffer 分配、端口签名），组件预编译为 DLL，`orpheus_runtime` / `orpheus_rt_host` LoadLibrary 经 C ABI 调用。编辑-运行循环零 C 编译。
2. **代码生成路径（UI「⚙ 编译后运行」）**：`cli generate` 展开为独立 C 工程（CMake），静态编译，无 DLL、无 Python 依赖，可交叉编译。

两条路径要求**逐字节一致**（有自动化一致性测试：`test_generated_run_matches_dynamic_run`）。

### 组件模型

- 每个组件有 `component.yaml` manifest：id/name/category、sources、ports、parameters（含 `update_policy`、`affects_signature`）、memory、execution。
- 端口可引用参数实现可变签名：`channels: param:channels`、`count: param:channels`，编译期展开（如 `out0..outN-1`）。影响签名的参数必须是 `restart_required`。
- 多端口组件：`ctx->inputs[]/outputs[]` 槽位按 manifest 端口声明顺序绑定；**未连接引脚为 NULL，process 必须判空**。
- 复合组件：工程文档内嵌 `subcomponents:`，编译前 `flatten_project` 递归展开为纯原子图；实例以 `component: "sub:<id>"` 引用。

### 运行时与宿主

- 时钟域：组件 manifest 声明 `clock_source: true` + `clock_domain`（device/file）；task 不显式建模，时钟源组件即时钟域根。
- 速率调整：`scheduling.divisor` 表达式让节点每 N 块触发一次（`downrate` / `resample` 组件）。
- `rt_host` 实时协议：stdin `SET <node> <param> <value>` / `GET` / `STOP`；stdout `LOG ...` 为生命周期日志，`PROBE <node> <param> <value>` 为探针上报。

## 常用命令（Windows PowerShell）

```powershell
python serve.py                       # 启动后端+UI：http://127.0.0.1:8000（同域 API + ui/build）
python -m orpheus_core.cli build      # 构建全部组件 + runtime（cmake -G Ninja）
python -m orpheus_core.cli compile <project.yaml>
python -m orpheus_core.cli generate <project.yaml>   # 生成独立 C 工程
python -m pytest orpheus_core/tests/  # 全部后端测试
cd ui; npm run build                  # 前端改动后必须重新构建，serve 才托管新版本
cd ui; npm start                      # 前端热更新（:3000，代理到 :8000 API）
```

## 开发红线（踩过的坑，违反必出 bug）

1. **实时路径禁令**：组件 `process` 中禁止 malloc/free/new/delete、阻塞锁、文件/网络 IO、printf、异常传播。日志只在 create/prepare/destroy/set_parameter 中输出；实时线程内输出走 PROBE 上报。
2. **编码**：源码一律 UTF-8 无 BOM + LF。禁止用 PowerShell `Get-Content`/`Set-Content` 改写源码（默认 GBK 会毁中文注释/字符串）；Python 子进程管道必须 `encoding="utf-8"`。
3. **工具链**：PATH 里 Perl 自带的 gcc 4.9.2 不可用；用 Strawberry GCC（以 `build/CMakeCache.txt` 的 `CMAKE_C_COMPILER` 为准；生成工程 configure 需显式传编译器）。
4. **组件入口符号**：组件 .c 的 ABI 入口必须用 `ORPHEUS_ENTRY_NAME` 宏包裹（缺省 `orpheus_get_interface`），否则静态链接多组件符号冲突崩溃。新组件照抄现有组件写法。
5. **设备回调周期 ≠ 图块长**：rt_host 回调里必须按 block_size 分块处理（参考 `rt_host.cpp`），设备周期请求下限 10ms。
6. **编码中文注释**：所有新代码注释/字符串按仓库惯例用中文。

## 典型工作流

- **加组件**：复制最相近的现有组件目录（如 `components/orpheus/builtin/gain/`）→ 改 id/name/manifest/实现 → `cli build` → 写示例工程验证（对照 `examples/`）。
- **改 UI**：编辑 `ui/src/` → `npm run build` → serve 托管即更新。参数控件走 `widgets.js` 注册表；节点本体定制走 `nodeWidgets.js`。
- **改编译/运行逻辑**：Python 侧改完跑 `pytest orpheus_core/tests/`；C++ 侧改完用 `cmake --build build` 重建 runtime/host 并跑相关 e2e/一致性测试。
- **改后端 API**：路由集中在 `orpheus_core/orpheus_core/server/app.py`（前缀 `/api`），测试在 `orpheus_core/tests/test_server*.py`。

## 测试约定

- 后端：`python -m pytest orpheus_core/tests/`（约 29 项，覆盖 server API、子组件展开、可变引脚、时钟/速率、双路径一致性）。
- C++：`tests/abi_smoke.c` ABI 冒烟测试；组件一致性靠 Python e2e 脚本（`scripts/`）。
- 新增组件应有数值正确性验证（示例工程或 pytest）。

## 深入阅读

- `docs/WHAT.md`：产品目标、核心需求、成功标准、非目标。
- `docs/HOW.md`：技术栈、架构、ABI、控制协议、代码生成、已实现特性（v0.1 ~ v1）。
- `docs/IMPLEMENTATION_PLAN.md` + `docs/implementation_log.md`：阶段计划与进度。
- `SKILL/SKILL.md`：开发技能（红线清单、任务索引）。
