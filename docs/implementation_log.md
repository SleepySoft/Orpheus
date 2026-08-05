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

