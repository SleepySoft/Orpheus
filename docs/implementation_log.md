# Orpheus 基础版本实施日志

## 2026-08-04

### 已完成

- 制定 `docs/IMPLEMENTATION_PLAN.md`，明确 8 个 Phase 与完成标准。
- 创建项目基础目录结构。
- Phase 1：契约与基础结构
  - 编写 `orpheus_abi/include/orpheus_abi.h`（ABI v1）。
  - 编写 `component_manifest.schema.json` 与 `project.schema.json`。
  - 编写顶层 `CMakeLists.txt`、ABI、组件目录 CMake。
  - 实现第一个组件 `orpheus.builtin.gain` 并成功编译为 DLL。

### 已完成

- 制定 `docs/IMPLEMENTATION_PLAN.md`，明确 8 个 Phase 与完成标准。
- 创建项目基础目录结构。
- Phase 1：契约与基础结构
  - 编写 `orpheus_abi/include/orpheus_abi.h`（ABI v1）。
  - 编写 `component_manifest.schema.json` 与 `project.schema.json`。
  - 编写顶层 `CMakeLists.txt`、ABI、组件目录 CMake。
  - 实现第一个组件 `orpheus.builtin.gain` 并成功编译为 DLL。
- Phase 2：Python Core
  - 实现 `Registry` 组件扫描与 manifest 解析。
  - 实现 `ProjectLoader` 工程加载/保存。
  - 实现 `GraphCompiler` 图编译（端口签名推导、连接校验、拓扑排序、执行计划生成）。
  - 实现 `ComponentBuilder` 调用 CMake 编译组件。
  - 实现 `orpheus-cli`（scan / compile / build / new）。
  - 验证：`scan` 发现 Gain、`compile` 生成 plan.json、`build` 生成 DLL。

### 已完成

- 制定 `docs/IMPLEMENTATION_PLAN.md`，明确 8 个 Phase 与完成标准。
- 创建项目基础目录结构。
- Phase 1：契约与基础结构
  - 编写 `orpheus_abi/include/orpheus_abi.h`（ABI v1）。
  - 编写 `component_manifest.schema.json` 与 `project.schema.json`。
  - 编写顶层 `CMakeLists.txt`、ABI、组件目录 CMake。
  - 实现第一个组件 `orpheus.builtin.gain` 并成功编译为 DLL。
- Phase 2：Python Core
  - 实现 `Registry` 组件扫描与 manifest 解析。
  - 实现 `ProjectLoader` 工程加载/保存。
  - 实现 `GraphCompiler` 图编译（端口签名推导、连接校验、拓扑排序、执行计划生成）。
  - 实现 `ComponentBuilder` 调用 CMake 编译组件。
  - 实现 `orpheus-cli`（scan / compile / build / new）。
  - 验证：`scan` 发现 Gain、`compile` 生成 plan.json、`build` 生成 DLL。
- Phase 3：C++ Runtime
  - 实现 `Plan::load_from_file`。
  - 实现跨平台 `ComponentLoader`（Windows DLL / Linux SO / macOS DYLIB）。
  - 实现 `Runtime`：实例创建、Buffer 分配、拓扑调度、参数设置、WAV 处理循环。
  - 实现 WAV 读写工具 `wav_io.h/cpp`。
  - 实现 `orpheus_runtime.exe` 命令行入口。
  - 跑通闭环：`WAV In -> Gain -> Biquad -> WAV Out`。

### 已完成

- 制定 `docs/IMPLEMENTATION_PLAN.md`，明确 8 个 Phase 与完成标准。
- 创建项目基础目录结构。
- Phase 1：契约与基础结构
  - 编写 `orpheus_abi/include/orpheus_abi.h`（ABI v1）。
  - 编写 `component_manifest.schema.json` 与 `project.schema.json`。
  - 编写顶层 `CMakeLists.txt`、ABI、组件目录 CMake。
  - 实现第一个组件 `orpheus.builtin.gain` 并成功编译为 DLL。
- Phase 2：Python Core
  - 实现 `Registry` 组件扫描与 manifest 解析。
  - 实现 `ProjectLoader` 工程加载/保存。
  - 实现 `GraphCompiler` 图编译（端口签名推导、连接校验、拓扑排序、执行计划生成）。
  - 实现 `ComponentBuilder` 调用 CMake 编译组件。
  - 实现 `orpheus-cli`（scan / compile / build / generate / new）。
- Phase 3：C++ Runtime
  - 实现 `Plan::load_from_file`。
  - 实现跨平台 `ComponentLoader`（Windows DLL / Linux SO / macOS DYLIB）。
  - 实现 `Runtime`：实例创建、Buffer 分配、拓扑调度、参数设置、WAV 处理循环。
  - 实现 WAV 读写工具 `wav_io.h/cpp`。
  - 实现 `orpheus_runtime.exe` 命令行入口。
  - 实现 `orpheus_rt_host.exe` 实时音频宿主（miniaudio 双工）。
- Phase 4：基础音频组件
  - Gain、Biquad、Mixer、Split、Merge、Delay、Signal Generator。
  - WAV In/Out、Device In/Out。
- Phase 5：监控组件
  - Probe RMS、Probe Peak、Probe Waveform。
- Phase 6：代码生成
  - 实现 `CodeGenerator`，从执行计划生成独立 C 工程。
  - 验证生成工程可编译通过 CMake。
- Phase 7：UI 基础
  - 创建 React + React Flow 骨架（`ui/`）。
  - 实现节点显示、连线、参数面板、运行按钮占位。

### 进行中

- Phase 8：端到端集成与音乐处理验证。

### 下一步

- 完善 UI 与 Python Core 的通信（HTTP/WebSocket 或 Tauri IPC）。
- 实现 UI 拖拽创建节点、参数编辑、工程保存。
- 运行生成的独立工程并验证输出与动态模式一致。
- 用真实音频设备运行 `orpheus_rt_host` 验证实时音乐加工。
- 补充自动化测试与使用文档。


---

## 2026-08-04（迭代 2~5：UI 落地与实时化）

### 迭代 2：UI 接入后端 + 工程工作区
- FastAPI 服务（`orpheus_core/server/`，`serve.py` / `orpheus-cli serve` 启动，单命令同域托管 UI+API）
- ProjectManager 内存工程模型，写穿到 `workspace/<name>/project.yaml`；zip 下载；示例导入（绝对 wav 路径自动改相对）
- 前端：组件面板/工程管理/防抖自动保存/编译/离线运行/产物试听；同步策略=编辑本地优先、整文档写回
- 子组件（复合组件）：文档内嵌 `subcomponents`，编译前 `flatten_project` 递归展开；UI 多视图标签页、框选包装、双击打开、端口编辑器

### 迭代 3：组件库组织 + 可变引脚
- manifest 增加 name（中文）/category；Palette 分类树（信号源/基础算法/通道路由/文件/设备/监控工具）
- 参数面板通用参数（channels 等 affects_signature）置顶分组
- 可变引脚：`count: param:channels` 编译期展开 `out0..N-1`；UI 改 channels 即时刷新引脚并清理悬挂连线
- 交错/反交错组件（AWE 风格通道映射即连线）替代 split/merge；Runtime 按端口 ID 精确绑定 Buffer
- 修复：gain 初始 gain_db 不生效；字符串参数被 atof 吞掉；输入引脚多重驱动校验（UI+编译器）

### 迭代 4：设备通路 + 控件定制 + 探针回读
- rt_host：`--list-devices` 设备枚举；device 参数选设备；loopback 模式（WASAPI 环回 + ma_pcm_rb 桥接）
- 参数控件架构：manifest `widget/options/options_source/readonly` + 前端 widgets.js 注册表；file 控件（工程内浏览/上传）；biquad 类型下拉；引脚通道徽标
- 探针回读：Runtime::get_parameter 透传；离线宿主 PROBE 行；nodeWidgets 注册表节点本体电平条

### 迭代 5：实时会话
- rt_host stdin 控制协议（SET/GET/STOP）+ 探针 200ms 轮询上报 + LOG 日志约定
- 后端 RtSession 子进程管理（rt/start|stop|status|param）；UI 实时运行/停止按钮、实时日志窗口、运行中调参即时生效、电平条每秒刷新
- 关键修复：设备回调周期大于 block_size 导致缓冲溢出崩溃（回调内分块）；MinGW 管道全缓冲；Python 管道迭代预读延迟

### 下一步
- 实时会话的设备图 UI 引导（含 device 节点时提示用实时运行）
- 调试视图：示波器波形可视化、运行中节点当场操作（暂停/复位）
- 生成模式与动态模式一致性自动化测试
- 连线即时 channel 校验（前端预检）
