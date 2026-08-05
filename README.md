# Orpheus

An intuitive, easily extensible audio processing framework based on visual programming, suitable for teaching and research, casual home use, and embeddable deployment.

## 文档

- [`docs/WHAT.md`](docs/WHAT.md) — 产品目标、核心需求、成功标准与范围边界。
- [`docs/HOW.md`](docs/HOW.md) — 技术栈、架构方案、关键机制与落地路线图。
- [`docs/design_draft.txt`](docs/design_draft.txt) — 历史设计草案与详细子系统分解。
- [`docs/design_v1.md`](docs/design_v1.md) — 高层概念草稿。

## 一句话介绍

在电脑上像剪映一样直观地搭建音频系统，生成的代码却像手工写的嵌入式模块一样干净可控。

## 快速开始（单命令模式）

```powershell
# 1. 安装 Python 工具链（含 HTTP 服务依赖）
pip install -e orpheus_core

# 2. 构建组件与运行时（需要 cmake + ninja + gcc）
python -m orpheus_core.cli build

# 3. 构建一次 UI 静态文件（之后前端不改就无需重复）
cd ui; npm install; npm run build; cd ..

# 4. 一条命令启动后端 + 网页，并自动打开浏览器
python serve.py --open        # 或: python -m orpheus_core.cli serve --open
# → http://127.0.0.1:8000 同时提供 API (/api) 和 UI (/)
```

PyCharm 调试：直接对根目录 `serve.py` 右键 Debug 即可，断点可命中 `orpheus_core/server/` 内所有路由与编译代码。

前端开发调试时改用热更新模式：`cd ui && npm start`（:3000，自动代理到 :8000 的 API）。

UI 使用流程：左上角「导入示例…」导入 `wav_gain_biquad` → 画布编辑（拖拽组件、连线、右侧改参数，1.5s 防抖自动保存 / Ctrl+S 手动保存）→ 「编译」→「▶ 运行」→ 底部试听输出 WAV → 「下载 zip」导出整个工程。

**子组件（复合组件）**：在画布中框选一组节点 → 工具栏「包装为子组件」→ 自动生成边界端口并替换为单个实例节点；**双击实例**在独立标签页中平铺打开内部图编辑（类 Simulink 子系统）；子组件属于当前工程，可拖拽复用、多层嵌套，编译时递归展开为原子图（Runtime 无感知）。

- 工程持久化在 `workspace/<工程名>/`（`project.yaml` 为唯一事实来源，已 gitignore）；WAV 路径相对工程目录，可移植。
- 组件是全局只读库（`components/` 扫描），工程是用户文档（`workspace/`），子组件定义内嵌于工程文档。
- 后端 API：`GET/PUT /api/projects/{name}`、`POST .../compile`、`POST .../run`、`GET .../download`、`GET /api/components` 等，见 `orpheus_core/server/app.py`。
- 后端测试：`python -m pytest orpheus_core/tests/`（server API + 子组件展开）。
