# Orpheus 基础版本实施计划

> 目标：在 PC 上运行一个能对播放音乐进行实时/文件加工的音频系统。所有模块（Runtime、Core、UI、代码生成、组件加载、基础音频组件、基础监控组件）齐全，但保持最小可用。
> 实施方式：分阶段推进，每阶段形成可运行闭环，并记录到 `docs/implementation_log.md`。

---

## 里程碑定义

**基础版本完成标准**：

1. 用户可以通过 UI 或命令行创建一个图：
   ```
   WAV Input / Device Input → Gain → Biquad → Mixer → WAV Output / Device Output
   ```
2. 图可以在 PC 上实时运行（设备 I/O）或离线运行（WAV 文件）。
3. 支持实时调整 Gain、Biquad 频率/增益等参数。
4. 支持 RMS/Peak/Waveform Probe 观察信号。
5. 同一工程可以生成独立 C/C++ 工程并编译运行。
6. 动态运行与生成工程输出一致（误差范围内）。
7. 组件有独立源码 + YAML metadata，可被动态编译加载。

---

## 阶段划分

### Phase 1：契约与基础结构

- 创建项目目录结构。
- 定义组件 ABI v1（C header）。
- 定义 `component.yaml` schema。
- 定义 `project.yaml` schema。
- 建立统一 CMake 构建系统（组件动态库、Runtime、测试）。
- 交付：可编译的 ABI header + 空 Runtime + 一个 Hello Component 模板。

### Phase 2：Python Core

- 实现组件 Registry（扫描、解析 manifest）。
- 实现 Project 数据模型（加载/保存 YAML）。
- 实现 Graph Compiler 最小版本：
  - 节点/端口解析
  - 参数化签名推导
  - 连接合法性检查
  - 拓扑排序
  - 执行计划（Execution Plan）生成
- 实现 Build Orchestrator（调用 CMake 编译组件动态库）。
- 交付：命令行工具 `orpheus-cli build` 和 `orpheus-cli compile`。

### Phase 3：C++ Runtime

- 实现动态库加载（Windows DLL / Linux SO / macOS DYLIB）。
- 实现组件实例生命周期（create/destroy/prepare/reset/process）。
- 实现 Buffer 管理（Signal / State / Scratch）。
- 实现 Scheduler（单 Task 拓扑执行）。
- 实现 WAV 文件输入输出。
- 实现 miniaudio 设备输入输出。
- 实现统一控制协议本地传输（In-process）。
- 交付：可运行 WAV → Gain → WAV。

### Phase 4：基础音频组件

首批实现：

- `orpheus.builtin.gain`
- `orpheus.builtin.biquad`
- `orpheus.builtin.mixer`
- `orpheus.builtin.split`
- `orpheus.builtin.merge`
- `orpheus.builtin.delay`
- `orpheus.builtin.wav_in`
- `orpheus.builtin.wav_out`
- `orpheus.builtin.device_in`
- `orpheus.builtin.device_out`
- `orpheus.builtin.signal_gen`

每个组件：C ABI、YAML、单元测试、Golden Vector。

### Phase 5：监控组件

- `orpheus.builtin.probe_rms`
- `orpheus.builtin.probe_peak`
- `orpheus.builtin.probe_waveform`
- Runtime Probe Service（非阻塞采集、限流、丢弃统计）。
- 控制协议读取 Probe 数据。

### Phase 6：代码生成

- 实现 Source Collector。
- 实现 Buffer / State / Schedule 生成。
- 实现 Task Entry 生成。
- 实现 CMake 生成。
- 实现生成工程编译与一致性比较。
- 交付：命令行工具 `orpheus-cli generate`。

### Phase 7：UI 基础

- React Flow 画布。
- 节点库（从 Registry 读取）。
- 拖拽创建节点。
- 类型化连线。
- 参数面板。
- 运行/停止按钮。
- Probe 图表（RMS、Waveform）。
- 与 Python Core 通过 HTTP/WebSocket 通信。

### Phase 8：端到端集成

- UI → Core → Runtime → 音频输出。
- 播放音乐加工验证。
- 实时参数调整听感验证。
- 生成工程验证。
- 编写集成测试与使用文档。

---

## 当前进度

见 `docs/implementation_log.md`。
