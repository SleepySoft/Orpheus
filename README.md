# Orpheus

An intuitive, easily extensible audio processing framework based on visual programming, suitable for teaching and research, casual home use, and embeddable deployment.

## 文档

- [`docs/WHAT.md`](docs/WHAT.md) — 产品目标、核心需求、成功标准与范围边界。
- [`docs/HOW.md`](docs/HOW.md) — 技术栈、架构方案、关键机制与落地路线图。
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — 当前实现状态、近期优先级与验收进度。
- [`docs/design_draft.txt`](docs/design_draft.txt) — 历史设计草案与详细子系统分解。
- [`docs/design_v1.md`](docs/design_v1.md) — 高层概念草稿。

## 一句话介绍

在电脑上像剪映一样直观地搭建音频系统，生成的代码却像手工写的嵌入式模块一样干净可控。

## 快速开始（单命令模式）

### 0. 安装构建工具链

构建需要 **CMake + Ninja + C/C++ 编译器**（编译器二选一）：

- **CMake**：https://cmake.org/download/（安装时勾选「Add CMake to system PATH」）。
- **Ninja**：`winget install Ninja-build.Ninja`（或 `pip install ninja`）。Ninja 不随 CMake / VS Build Tools 自动加入 PATH；缺它时 `cli build` 会报 `ninja: not found`。
- **编译器**，二选一：
  - **方案 A（MinGW，推荐）**：安装 Strawberry Perl（自带 GCC）：https://strawberryperl.com/ ，并把 `C:\Strawberry\c\bin` 加入 PATH。注意：PATH 里 Perl 自带的老 gcc 4.9.2 不可用，请用 Strawberry 的版本。
  - **方案 B（MSVC）**：Visual Studio 2022 Build Tools：https://my.visualstudio.com/Downloads?q=visual%20studio%202022&wt.mc_id=o~msft~vscom~older-downloads（勾选「使用 C++ 的桌面开发」）。**首次**配置请在「x64 Native Tools Command Prompt for VS 2022」中运行一次 `python -m orpheus_core.cli build`；之后任意终端均可——`cli build` 检测到 MSVC 会自动加载 VS 环境（`vcvars64.bat`），无需手动切换。项目已支持 MSVC 构建（UTF-8 源码与 DLL 命名已兼容）。

仓库完整验证固定使用 **Python 3.12 + Node.js 20 + MSVC x64**。安装这些工具后可在普通 PowerShell 中运行：

```powershell
./scripts/verify.ps1 -Python C:\path\to\python.exe
```

该脚本依次安装依赖、构建组件/runtime/C 测试工具、运行 CTest 与 pytest、运行前端测试并生成生产构建；Windows CI 执行同一入口。Python 包本身仍声明兼容 3.10+，但提交前基线以 3.12 为准。

```powershell
# 1. 安装 Python 工具链（含 HTTP 服务依赖）
pip install -e orpheus_core

# 2. 构建组件与运行时（需要 cmake + ninja + 编译器，见上）
python -m orpheus_core.cli build

# 3. 构建一次 UI 静态文件（之后前端不改就无需重复）
cd ui; npm install; npm run build; cd ..

# 4. 一条命令启动后端 + 网页，并自动打开浏览器
python serve.py --open        # 或: python -m orpheus_core.cli serve --open
# → http://127.0.0.1:8000 同时提供 API (/api) 和 UI (/)
```

PyCharm 调试：直接对根目录 `serve.py` 右键 Debug 即可，断点可命中 `orpheus_core/server/` 内所有路由与编译代码。

前端开发调试时改用热更新模式：`cd ui && npm start`（:3000，自动代理到 :8000 的 API）。

UI 使用流程：左上角「导入示例…」导入示例工程 → 画布编辑（拖拽组件、连线、右侧改参数，1.5s 防抖自动保存 / Ctrl+S 手动保存）→「编译」检查图 → 两种运行方式：

- **▶ 运行**（基座动态加载）：按图内容自动分流——含设备组件的图进入实时会话（声卡/系统声音，底部实时日志滚动，**运行中调参即时生效**，探针电平每秒刷新，「■ 停止」结束）；纯文件图走离线处理（WAV 进 WAV 出，产物在线试听/下载）。输入输出自由组合：系统声音→处理→WAV 就是录制，WAV→处理→声卡就是播放。
- **⚙ 编译后运行**（代码生成路径）：生成独立 C 工程 → 静态编译 → 运行；与动态加载路径的输出有逐字节一致性测试保障。

「下载 zip」可导出整个工程目录。

**设备通路**：`音频采集`/`设备输出` 组件支持选择设备（下拉，含虚拟声卡如 VB-Cable）；采集源可选「系统声音（Loopback）」拦截其他应用播放的音频进图处理。

**子组件（复合组件）**：在画布中框选一组节点 → 工具栏「包装为子组件」→ 自动生成边界端口并替换为单个实例节点；**双击实例**在独立标签页中平铺打开内部图编辑（类 Simulink 子系统）；子组件属于当前工程，可拖拽复用、多层嵌套，编译时递归展开为原子图（Runtime 无感知）。

- 工程持久化在 `workspace/<工程名>/`（`project.yaml` 为唯一事实来源，已 gitignore）；WAV 路径相对工程目录，可移植。
- 组件是全局只读库（`components/` 扫描），工程是用户文档（`workspace/`），子组件定义内嵌于工程文档。
- 后端 API：`GET/PUT /api/projects/{name}`、`POST .../compile`、`POST .../run`、`GET .../download`、`GET /api/components` 等，见 `orpheus_core/server/app.py`。
- 后端测试：`python -m pytest orpheus_core/tests/`（server API + 子组件展开）。
