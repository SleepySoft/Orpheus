# Orpheus — 怎么做（HOW）

> 本文回答：Orpheus 用什么技术、如何分层、关键机制如何实现、分几个阶段落地。它是 `WHAT.md` 的技术实现方案。

---

## 1. 技术栈决策

| 层级 | 选型 | 说明 |
|---|---|---|
| 图形界面 | **TypeScript + React + React Flow** | React Flow 提供自由画布、多端口、类型化连线、子图、分组，风格接近 ComfyUI，适合“中式直观”体验 |
| 桌面容器 | **Tauri**（v2） | 承载 Web UI，管理本地进程与文件权限；第一阶段允许先用浏览器开发，稳定后再打包 |
| 工程编排/构建/代码生成 | **Python 3.12+** | 工程读写、Schema 校验、组件扫描、CMake 调用、Runtime 启动、控制客户端、代码生成 |
| 实时运行时 | **C++11** | 组件加载、Buffer 管理、调度、控制、Probe、音频 I/O；对外提供 C ABI |
| 组件实现 | **优先 C11，允许 C++11** | 必须暴露 C ABI，不得跨 ABI 传 C++ 对象/STL/异常；C++11 为嵌入式兼容上限 |
| 音频后端 | **miniaudio** | 轻量、跨平台、单头文件，适合 Runtime 与嵌入式生成代码解耦；JUCE 仅作为可选 PC 宿主参考 |
| 构建系统 | **CMake + Ninja** | 组件动态库、Runtime、生成工程、测试统一使用 |
| 工程描述 | **YAML** | 人类可读、Git diff 友好 |
| Schema 校验 | **JSON Schema** | 校验 Project、Component Manifest、Target Profile |
| 运行中间表示 | **二进制 IR（JSON/MsgPack 描述 + 可选二进制 blob）** | Runtime 加载编译后的执行计划，避免运行时解析 YAML |
| 控制协议 | **自定义二进制消息协议** | 请求/响应/事件/流数据分离，与 Transport 解耦 |
| 测试 | **GoogleTest（C++）+ pytest（Python）+ Playwright（UI）** | 单元、集成、端到端、Golden Vector |
| 代码质量 | **Clang-Tidy、Cppcheck、AddressSanitizer、ThreadSanitizer** | 静态分析与动态检测 |
| 文档 | **Markdown** | 与代码仓库共存 |

### 1.1 为什么这样选

- **React Flow + Tauri**：比传统 Simulink/Audio Weaver 更自由、更现代，符合“剪映式”直观操作；同时保留桌面应用能力。
- **Python 做编排**：Python 在构建脚本、CMake 调用、代码生成、测试编排上生态成熟，且**不进入实时音频回调**。
- **miniaudio 替代 JUCE**：Runtime 需要尽量轻量，避免把 JUCE 的 GUI/插件生态引入核心；miniaudio 单头文件、跨平台、支持 WASAPI/CoreAudio/ALSA/JACK。
- **C ABI 作为组件边界**：保证 C/C++ 组件、动态库、生成静态工程、外部目标平台之间的一致性。

---

## 2. 总体架构

```
┌─────────────────────────────────────────────┐
│  Presentation Layer                         │
│  React + React Flow（图编辑器 / 参数面板 /    │
│  Probe 可视化 / 教学视图 / 调试视图）          │
└───────────────┬─────────────────────────────┘
                │ HTTP / WebSocket / Tauri IPC
┌───────────────▼─────────────────────────────┐
│  Application Service                        │
│  Python Orchestrator                        │
│  Project / Registry / Build / Generator     │
│  Control Client                             │
└───────┬───────────────┬─────────────────────┘
        │               │
┌───────▼───────┐   ┌───▼─────────────────┐
│ Build Toolchain│   │ Control Protocol     │
│ CMake + Ninja  │   │ Local / External     │
└───────┬───────┘   └───┬─────────────────┘
                        │
┌───────────────────────▼─────────────────────┐
│  C++ Runtime                                  │
│  Plan Loader / Scheduler / Buffer Manager     │
│  Control Service / Probe Service / Audio I/O  │
└───────────────┬─────────────────────────────┘
                │ Component ABI (C)
┌───────────────▼─────────────────────────────┐
│  C/C++ Component Library                    │
│  DSP / Routing / Resampler / Bridge / Debug │
└─────────────────────────────────────────────┘
```

### 2.1 分层依赖规则

```
UI → Application Service → Domain Model → Runtime Contract / Build Contract → Component ABI
```

- Domain Model 不依赖 React。
- Component ABI 不依赖 Python。
- Runtime 不依赖 UI。
- DSP 组件不依赖具体调度器实现。
- 控制协议不依赖具体通信方式。
- 代码生成器不依赖 PC 音频设备。

---

## 3. 组件模型与 ABI

### 3.1 组件目录结构

```
components/<namespace>/<category>/<name>/<version>/
├── component.yaml      # metadata
├── include/
│   └── orpheus_gain.h  # 公开 C ABI 头
├── src/
│   ├── gain.c          # 算法实现
│   └── gain_abi.c      # ABI 适配（可选合并）
├── tests/
│   ├── test_gain.cpp
│   └── golden/
├── docs/
│   └── README.md
└── ui/
    └── icons/
```

### 3.2 Component Manifest（component.yaml）

```yaml
id: orpheus.builtin.gain
version: 1.0.0
abi_version: 1
sources:
  - src/gain.c
  - src/gain_abi.c
headers:
  - include/orpheus_gain.h
ports:
  - id: in
    direction: input
    type: audio
    sample_format: f32
    channels: param:channels
  - id: out
    direction: output
    type: audio
    sample_format: f32
    channels: param:channels
parameters:
  - id: gain_db
    type: float
    default: 0.0
    range: [-96.0, 24.0]
    unit: dB
    update_policy: smoothed
    smoothing_time_ms: 10
  - id: channels
    type: int
    default: 2
    range: [1, 32]
    update_policy: restart_required
memory:
  state_size: 0
  scratch_size: 0
  alignment: 8
execution:
  sample_rate_independent: true
  latency_samples: 0
  supports_inplace: true
  realtime_safe: true
```

### 3.3 C ABI v1（核心接口）

```c
// orpheus_abi.h
#define ORPHEUS_ABI_VERSION 1

typedef struct OrpheusComponentDescriptor {
    const char* id;
    const char* version;
    uint32_t abi_version;
    const OrpheusPort* ports;
    size_t port_count;
    const OrpheusParameter* params;
    size_t param_count;
    size_t state_size;
    size_t scratch_size;
    size_t alignment;
} OrpheusComponentDescriptor;

typedef struct OrpheusProcessContext {
    void* state;
    const OrpheusBuffer** inputs;
    OrpheusBuffer** outputs;
    size_t frame_count;
    void* scratch;
    const OrpheusEvent* events;
    size_t event_count;
} OrpheusProcessContext;

typedef struct OrpheusComponentInterface {
    const OrpheusComponentDescriptor* (*get_descriptor)(void);
    int (*prepare)(void* state, const OrpheusConfig* config);
    int (*reset)(void* state);
    int (*process)(void* state, const OrpheusProcessContext* ctx);
    int (*set_parameter)(void* state, uint32_t param_id, const OrpheusValue* value);
    int (*get_parameter)(void* state, uint32_t param_id, OrpheusValue* value);
} OrpheusComponentInterface;

ORPHEUS_EXPORT const OrpheusComponentInterface* orpheus_get_interface(void);
```

### 3.4 参数更新策略

| 策略 | 说明 |
|---|---|
| `immediate` | 立即写入，当前块即生效 |
| `block_boundary` | 在下一块边界生效，保证无 glitch |
| `smoothed` | 在指定时间常数内平滑过渡 |
| `transactional` | 多参数事务，统一边界提交 |
| `restart_required` | 改变后需要重新初始化组件 |

### 3.5 组件包类型

组件以**包（Package）**为单位发布，支持两种形态：

| 类型 | 说明 | 适用场景 |
|---|---|---|
| **Source Package** | 包含 `component.yaml` + 源码 + 头文件 + 测试 | PC 动态编译、目标静态编译、学习阅读源码 |
| **Binary Package** | 包含 `component.yaml` + 预编译库（`.a` / `.lib` / `.so`）+ 头文件 | 保护知识产权、加速构建、第三方闭源组件 |

Binary Package 的 `component.yaml` 必须声明：

```yaml
package_type: binary
binaries:
  - platform: windows-x86_64
    compiler: msvc19
    artifact: lib/gain_x64.lib
  - platform: generic-arm
    compiler: gcc-arm-none-eabi
    artifact: lib/gain_arm.a
```

两种包在图编译、执行计划、控制协议、代码生成阶段使用相同的 metadata；构建系统根据当前目标选择源码编译或链接预编译库。

### 3.6 分层组件与嵌套组合

组件可以像芯片模块一样逐层堆叠：

```
Top System
├── Subsystem: Crossover
│   ├── Deinterleave (2ch)
│   ├── Biquad (LP)
│   └── Biquad (HP)
├── Subsystem: EQ Bank
│   ├── Biquad (Peak)
│   └── Biquad (High Shelf)
└── Mixer
```

组合规则：

- **原子组件（Atomic Component）**：由 C/C++ 源码或二进制库实现，不可再展开。
- **复合组件（Composite Component / Subsystem）**：内部包含 Graph，可展开为平面图；也可封装为独立组件复用。
- **复合组件的公开端口和参数**通过显式映射声明，内部细节对上层隐藏。
- 图编译时，结构型复合组件必须完全展开；封装型复合组件可作为独立编译单元。
- 嵌套层级不影响端口类型检查，错误可定位到原始节点路径（如 `Top/Crossover/Biquad_LP`）。

复合组件同样使用 `component.yaml` 描述，区别是包含 `graph` 字段而非 `sources`：

```yaml
id: myteam.audio.crossover
version: 1.0.0
package_type: composite
graph:
  nodes:
    - id: deint
      component: orpheus.builtin.deinterleave
      params: { channels: 2 }
    - id: lp
      component: orpheus.builtin.biquad
      params: { type: lowpass, fc: 1000, channels: 1 }
  connections:
    - from: @in
      to: deint:in
    - from: deint:out0
      to: lp:in
    - from: lp:out
      to: @out0
public_ports:
  - id: in
    direction: input
    binds_to: deint:in
  - id: out0
    direction: output
    binds_to: lp:out
```

### 3.7 参数化端口与可变签名

通用音频组件的接口签名（通道数、采样格式、块长度等）通常由参数决定。例如：

- `Gain` 的 `channels` 参数改变时，输入/输出端口通道数同步改变。
- `Mixer` 的 `inputs` 参数改变时，输入端口数量同步改变。
- `Resampler` 的 `ratio` 参数改变时，输出采样率同步改变。

设计要点：

1. **端口签名表达式**：`component.yaml` 中端口属性可引用参数，如 `channels: param:channels`、`sample_rate: param:output_rate`。
2. **签名解析时机**：图编译阶段，根据节点参数实例值解析出确定性签名，生成执行计划。
3. **签名变更触发**：改变影响端口签名的参数属于 `restart_required` 策略，必须重新编译执行计划。
4. **连接校验**：编译器比较两端端口签名（格式、通道数、采样率、块长、时钟域），不兼容时给出明确诊断。
5. **多态端口**：支持可变数量端口（如 Mixer 的多输入），通过参数 `port_count` 声明。

示例：

```yaml
parameters:
  - id: channels
    type: int
    default: 2
    range: [1, 32]
    update_policy: restart_required
ports:
  - id: in
    direction: input
    type: audio
    sample_format: f32
    channels: param:channels
  - id: out
    direction: output
    type: audio
    sample_format: f32
    channels: param:channels
```

### 3.8 构建时元数据推断

Source Package 在构建后，应能从编译产物中自动提取或验证 metadata：

```
component.yaml  （声明式 metadata）
      ↓
构建 Source Package → 动态库 / 静态库
      ↓
Metadata Extractor 读取 ABI 导出符号
      ↓
生成 inferred_manifest.json
```

推断内容：

- 端口数量与方向（通过 ABI `get_descriptor`）。
- 状态内存大小、对齐要求（通过 ABI 查询）。
- 参数列表与默认值（通过 ABI 查询）。
- 实际支持的采样率、块长度范围。

用途：

- 校验 `component.yaml` 声明与实现是否一致。
- 为 Binary Package 自动生成 metadata（无源码时）。
- 在图编译阶段使用确定性签名。

---

## 4. Runtime 设计

### 4.1 Runtime 内部模块

```
Runtime
├── Plan Loader          # 加载编译后的 Execution Plan
├── Component Loader     # 动态加载 DLL/SO/DYLIB
├── Instance Manager     # 组件实例生命周期
├── Buffer Manager       # Signal / State / Scratch 内存
├── Scheduler            # Task 内拓扑执行
├── Audio Backend        # miniaudio 适配
├── Control Service      # 统一控制协议服务
├── Probe Service        # Probe 采集与限流
└── Statistics Service   # CPU、Buffer Level、Deadline
```

### 4.2 实时路径禁令

实时处理函数与回调中禁止：

- `malloc` / `free` / `new` / `delete`
- 阻塞锁、文件 I/O、网络 I/O
- 日志格式化、UI 回调
- 异常传播

### 4.3 内存规划

三种内存类型：

| 类型 | 说明 | 生命周期 |
|---|---|---|
| Persistent / State | 滤波历史、延迟线、包络状态 | 工程运行期间持续 |
| Signal Buffer | 组件间传输数据 | 按执行计划生命周期 |
| Scratch | 单次 process 临时内存 | 单次 process，可跨节点复用 |

规划阶段输出：

- 总内存、每类内存、每组件内存、每 Task 内存。
- 对齐损耗、峰值 Scratch、Buffer 生命周期。
- 目标内存区域映射（SRAM / DDR / TCM 等）。

---

## 5. 图形编辑器设计

### 5.1 前端状态分层

- **Project Semantic State**：节点、连接、参数、Task、Probe 等。
- **Presentation State**：画布位置、缩放、选中、折叠、颜色主题。
- **Runtime State**：运行/停止/错误、参数反馈、Probe 数据。
- **Temporary Interaction State**：拖拽、连线预览、上下文菜单。

### 5.2 与后端交互

- 编辑操作 → Python Orchestrator Project API。
- 运行控制 → Python Control Client → Runtime Control Service。
- Probe 数据 → Control Protocol Stream Packet → React 可视化组件。

### 5.3 视觉风格

- 节点卡片化，端口在左右两侧，参数可内联编辑。
- 不同 Task 的节点用不同颜色/边框高亮。
- 跨 Task 的连接通过 Task Bridge 组件显示为“半色”连线。
- 非法连接即时红色提示并给出修复建议。
- 三种视图切换：教学视图（简化）、工程视图（完整）、调试视图（Probe + 统计）。

---

## 6. 工程编译与执行计划

### 6.1 图编译 Pass

```
1. Parse Pass              # 解析节点、端口、参数
2. Reference Resolution   # 连接引用解析
3. Type Inference         # 端口类型、通道数、采样率推导
4. Subsystem Expansion    # 展开结构型子系统
5. Domain Analysis        # 划分 Task Domain / Rate Domain / Clock Domain
6. Connection Validation  # 类型、采样率、跨域合法性
7. Cycle Validation       # 检测非法反馈环
8. Scheduling             # 每个 Task 内拓扑排序
9. Memory Planning        # 分配 State / Signal / Scratch
10. Registry Generation   # 参数、Probe、Control 注册表
11. Execution Plan Output # 输出二进制 IR
```

### 6.2 Execution Plan 内容

```json
{
  "abi_version": 1,
  "tasks": [
    {
      "id": 0,
      "sample_rate": 48000,
      "block_size": 128,
      "nodes": ["n0", "n1", "n2"],
      "execution_order": ["n0", "n1", "n2"]
    }
  ],
  "nodes": {
    "n0": { "component": "orpheus.builtin.wav_in", "params": {} }
  },
  "buffers": { ... },
  "memory_layout": { ... },
  "parameter_registry": { ... },
  "probe_registry": { ... }
}
```

---

## 7. 代码生成策略

### 7.1 生成目录结构

```
generated/
├── CMakeLists.txt
├── include/
│   └── orpheus_config.h
├── src/
│   ├── main.c              # 可选 PC 可执行入口
│   ├── task_entries.c      # 任务入口与执行列表
│   ├── buffers.c           # Buffer 与内存布局
│   ├── control_registry.c  # 参数注册表
│   └── probe_registry.c    # Probe 注册表
├── components/
│   └── orpheus_builtin_gain/  # 复制组件源码
├── platform/
│   └── generic/            # 平台适配层
├── tests/
└── reports/
    ├── memory_report.md
    └── latency_report.md
```

### 7.2 生成原则

1. **源码复制**：组件算法源码原样复制到 `components/`。
2. **桥接生成**：实例化、连接、任务入口、注册表由生成器按模板生成。
3. **确定性**：相同输入、组件版本、Target Profile 产生相同输出。
4. **可读性**：生成代码人工可读，命名稳定，便于在目标芯片上调试。
5. **无 Python 依赖**：生成工程独立编译。

---

## 8. 控制协议与 Transport

### 8.1 消息类型

- **Request**：参数读写、生命周期、Bulk 操作、Probe 配置。
- **Response**：请求响应，含错误码。
- **Event**：运行状态变化、错误、任务统计。
- **Stream Packet**：Probe 数据、日志、性能计数。

### 8.2 消息头

```c
typedef struct {
    uint16_t protocol_version;
    uint16_t message_type;     // Request / Response / Event / Stream
    uint32_t request_id;
    uint32_t session_id;
    uint32_t payload_length;
    // 完整性校验字段（CRC32 或更轻量校验）
} OrpheusMessageHeader;
```

### 8.3 Transport 接口

```c
typedef struct {
    const char* name;
    size_t max_packet_size;
    bool reliable;
    bool ordered;
    bool full_duplex;
    int (*open)(void* ctx);
    int (*close)(void* ctx);
    int (*send)(void* ctx, const uint8_t* data, size_t len);
    int (*recv)(void* ctx, uint8_t* buf, size_t* len);
    int (*query_status)(void* ctx);
} OrpheusTransportInterface;
```

核心框架提供：

- In-process Transport（本地 Runtime 直接调用）。
- Loopback Transport（测试）。
- Shared Memory Transport（跨进程低延迟）。

外部实现：TCP、UART、SWD、厂商链路。

---

## 9. 多任务与桥接

### 9.1 Task 模型

```yaml
tasks:
  - id: 0
    name: audio_processing
    sample_rate: 48000
    block_size: 128
    priority: high
    core_affinity: [0]
    target_entry: process_audio
  - id: 1
    name: slow_control
    sample_rate: 1000
    block_size: 1
    priority: low
    target_entry: process_control
```

### 9.2 桥接矩阵

| 场景 | 需要的组件 |
|---|---|
| 同 Task、不同 Block Size | Rebuffer / Block Splitter / Accumulator |
| 同 Task、不同 Sample Rate | Resampler |
| 不同 Task、同 Sample Rate | Task Bridge + Ring Buffer |
| 不同 Task、不同 Sample Rate | Task Bridge + Resampler |
| 不同 Clock Domain | Async Bridge + 水位监控 + 漂移处理 |

### 9.3 Task Bridge

- 使用 SPSC Ring Buffer 作为默认跨 Task 缓冲。
- 声明输入/输出速率、块长、缓冲容量、固定延迟、最大延迟。
- 欠载/溢出策略：zero-fill / hold-last / mute / report。
- 提供水位 Probe。

---

## 10. 目标平台适配

### 10.1 Target Profile

```yaml
id: generic_bare_metal
cpu: cortex-m7
compiler: gcc-arm-none-eabi
sample_format: q31
memory_regions:
  - name: dtcm
    size: 0x80000
    alignment: 8
  - name: sdram
    size: 0x800000
    alignment: 32
max_tasks: 8
thread_model: none
critical_section: disable_interrupts
timestamp: dwt_cyccnt
transport_binding: external
```

### 10.2 平台适配层（platform/）

```c
orpheus_platform_init();
orpheus_platform_critical_section_enter();
orpheus_platform_critical_section_exit();
orpheus_platform_timestamp_us();
orpheus_platform_task_register(...);
orpheus_platform_memory_section_bind(...);
```

---

## 11. 测试策略

| 层级 | 工具 | 内容 |
|---|---|---|
| 组件单元 | GoogleTest | 每个组件数值正确性、边界、参数 |
| ABI 合规 | GoogleTest | 动态/静态调用一致性、多实例 |
| 图编译器 | pytest | 合法/非法图、诊断、稳定执行顺序 |
| Runtime 集成 | pytest + C++ | 离线 WAV、实时回调、启停资源 |
| 控制协议 | pytest | 请求/响应、会话、错误码 |
| UI 端到端 | Playwright | 创建、连接、运行、监听 |
| 生成工程 | pytest | 生成 CMake 可编译、与动态模式一致 |
| 长时间稳定性 | pytest | Ring Buffer、桥接、内存 |
| 静态/动态检测 | Sanitizer / Clang-Tidy | 内存、线程、未定义行为 |

---

## 12. 开发路线图

### 阶段一：契约冻结（4~6 周）

- Project Schema、Component Manifest Schema、ABI v1、Port Type System、Parameter Model、Execution Domain Model、Probe Model、Control Protocol、Target Profile。
- 交付：Schema 文件、ABI Header、概念说明、合法/非法样本、设计测试。

### 阶段二：命令行最小闭环（4~6 周）

- 组件 Registry、Gain 组件、图编译器最小版本、组件构建器、PC Runtime 离线模式、WAV I/O、单 Task 代码生成、结果一致性测试。
- 目标闭环：`YAML Graph → 构建 Gain → Runtime 处理 WAV → 生成静态工程 → 编译运行 → 比较输出`。

### 阶段三：实时运行闭环（4~6 周）

- miniaudio Audio Backend、实时 Runtime、Local Control Transport、参数注册表、Block Boundary Commit、RMS/Waveform Probe、Runtime 统计。
- 目标闭环：`音频输入 → Gain → Biquad → 输出 → 调参 → 下一 Block 生效 → UI/CLI 读取 Probe`。

### 阶段四：图形编辑闭环（4~6 周）

- React Flow Canvas、节点库、连线、参数面板、诊断、运行工具栏、Probe 面板、工程保存、子系统基础。
- 目标闭环：`拖入组件 → 连接 → 配置 → 运行 → 听音 → 调参 → 看波形 → 保存`。

### 阶段五：基础 DSP 与代码生成（4~6 周）

- Mixer、Interleave/Deinterleave、FIR、Biquad、Rebuffer、Resampler、内存规划、控制/Probe 注册表生成、生成报告。

### 阶段六：多任务（4~6 周）

- Task Domain、多 Task Execution Plan、SPSC Ring Buffer、Task Bridge、Rebuffer 跨任务、多 Task 代码生成、Deadline/水位 Probe、长时间稳定性测试。

### 阶段七：目标接入与教学（4~6 周）

- Target Profile、Generic Bare-metal/RTOS Adapter、External Transport ABI、Bulk Transfer、Teaching Package、教学视图、自动检查、教师内容编辑工具。

---

## 13. 关键设计原则

1. **先冻结契约，再实现功能**。Project / ABI / Control Protocol 是上层模块唯一依赖。
2. **UI 与 Runtime 完全解耦**。所有交互走统一控制协议。
3. **实时路径极简**。无堆分配、无阻塞、无异常。
4. **生成代码保持可读**。复杂度留在框架和组件，不在生成代码中叠加。
5. **PC 动态模式与生成模式结果一致**。任何算法改动必须同时通过两种模式验证。
6. **文本化、版本友好**。工程与组件 metadata 均为 YAML/Markdown，便于 Git 管理。
7. **组件自包含**。每个组件可独立编译、独立测试、独立版本化。

---

## 14. 文档关系

- `WHAT.md`：产品定义与需求基准。
- `HOW.md`（本文）：技术栈、架构与实现方案。
- `design_draft.txt`：历史设计草案与详细子系统分解。
- `design_v1.md`：高层概念草稿。


---

## 15. UI 后端服务（已实现 v0.1）

当前实现的后端与持久化模型，对应「UI 与 Runtime 完全解耦」原则的落地形态：

- **HTTP 服务**：`orpheus-cli serve`（FastAPI + uvicorn，默认 `127.0.0.1:8000`），代码在 `orpheus_core/server/`。API 前缀 `/api`：`components`（全局只读组件库）、`projects`（工程 CRUD / compile / run / files / download）、`examples`（可导入示例）。
- **内存模型**：`Registry` 启动时扫描缓存组件；`ProjectManager` 持有 `dict[name -> ProjectRecord]`，工程以 `Project` 对象常驻内存，读写、编译、运行均直接操作内存对象。
- **同步策略**：前端编辑态本地优先（React 状态），整文档写回；触发时机为 1.5s 防抖自动保存 + Ctrl+S/保存按钮 + 运行前强制保存。后端写穿（write-through）：PUT 即校验并落盘。
- **持久化**：`workspace/<工程名>/project.yaml` 为唯一事实来源，`project.plan.json` 与 `outputs/` 为可再生产物；`GET /api/projects/{name}/download` 打包 zip 下载。工程内 WAV 路径相对工程目录（运行子进程 `cwd` 设为工程目录），导入示例时自动把绝对路径改写为相对路径并拷贝输入文件。
- **实体区分**：组件是全局只读库（`components/` 扫描），工程是用户文档（`workspace/`）；两类实体分离建模，UI 分别以组件面板和工程选择器呈现。

---

## 16. 工程内子组件（已实现 v1）

- **模型**：子组件定义内嵌在工程文档 `subcomponents:` 键中（id / ports / graph），工程私有；实例以 `component: "sub:<id>"` 引用。
- **边界端口**：每个对外端口声明 `maps_to: "内部原子节点:端口"`；v1 要求映射到原子节点端口，不做参数提升（mask）。
- **编译时展开（Flatten）**：`orpheus_core.subgraph.flatten_project()` 在编译前把 `sub:` 实例递归展开为纯原子图（内部节点 id 加 `<实例>__` 前缀、边界连接按 maps_to 重接），编译器/Runtime/代码生成对层级完全无感知。校验：未定义引用、循环引用、非法 maps_to、重复端口 id 均抛 CompileError。
- **UI**：多视图标签页（主图 + 每个打开的子组件一个平铺标签，无层级嵌套显示）；框选节点 →「包装为子组件」自动推导边界端口；双击实例打开子组件标签；子组件视图右侧可编辑接口端口。

## 17. 单命令启动（已实现）

`orpheus-cli serve [--open]`：FastAPI 在 API 路由之外托管 `ui/build` 静态文件（存在时），`http://127.0.0.1:8000` 同域提供 UI 与 `/api`；前端 `api.js` 按端口自动选择 baseURL（:3000 开发模式走 CORS 直连 :8000，同域模式走相对 `/api`）。

---

## 18. 可变引脚与交错/反交错（已实现 v1）

- **设计理念（借鉴 AWE）**：通道映射即连线。`deinterleave`（反交错器）把一路 N 通道交错信号拆成 N 路单声道输出；`interleave`（交错器）反之。通过连线即可选择/放弃/复制/交换通道，取代了固定的 split/merge 组件（已移除）。未连接的 interleave 输入引脚输出静音。
- **可变引脚（Variable Pins）**：manifest 端口支持 `count: <expr>`（如 `param:channels`），编译时展开为 `<id>0..<id>N-1`；前端 `resolvePorts()` 做同样展开，**修改 channels 参数会即时刷新引脚数量**并清理悬挂连线。
- **端口精确绑定**：plan 的 node_configs 携带 `input_ports`/`output_ports` 有序端口列表，Runtime 按端口 ID 建立索引映射绑定 Buffer（替代早期按连接顺序的绑定），未连接引脚为 nullptr，组件需判空。
- **初始参数**：组件在 `prepare` 时从 `OrpheusConfig.param_ids/param_values` 读取初始参数；Runtime 按「纯数字→FLOAT，否则→STRING」转换参数值（修复了 gain_db 初始值不生效、字符串参数被 atof 吞掉的问题）。
- 示例：`examples/wav_channel_map.yaml`（交换左右声道 + 单通道增益，数值验证通过）。

---

## 19. 设备通路、参数控件与探针回读（已实现 v1）

- **设备选择 + Loopback**：rt_host 支持 `--list-devices`（JSON 输出，供 `GET /api/devices` 使用）；device_in/device_out 新增 `device` 参数（设备名子串匹配，空=默认设备）；device_in 的 `source` 可选 `microphone`（duplex）或 `loopback`（WASAPI 环回采集系统混音，`ma_pcm_rb` 环形缓冲桥接到播放设备主时钟）。配合 VB-Cable 等现成虚拟声卡即可做应用间路由（不自研内核驱动）。
- **参数控件定制**：manifest 参数支持 `widget`（number/text/slider/select/checkbox/file，缺省按 type 推断）、`options`、`options_source`（动态下拉如设备列表）、`readonly`。前端 `widgets.js` 为控件注册表，新组件个性化控件 = 注册 widget + manifest 声明。file 控件走工程内文件浏览/上传（`POST /api/projects/{name}/uploads`），保持工程可移植。
- **探针回读**：probe 组件 readback 参数经 `Runtime::get_parameter` 透传；离线宿主跑完打印 `PROBE <node> <param> <value>`，run 响应携带 `probes`；前端 `nodeWidgets` 注册表按组件 id 定制节点本体（电平条）。运行中实时回读/节点当场操作待实时宿主 UI 化后提供。

---

## 20. 实时会话与控制协议（已实现 v1）

- **架构**：`POST /api/projects/{name}/rt/start` 由后端拉起 `rt_host` 子进程（stdin/stdout 管道），`RtSession` 读线程解析输出；UI 轮询 `rt/status` 刷新日志与探针值。
- **stdin 控制协议**：`SET <node> <param> <value>`（运行中调参，OK/ERR 回显）、`GET <node> <param>`（VALUE 回显）、`STOP`（或回车/stdin EOF 退出）。
- **日志机制**：约定 stdout 行为日志流——`LOG ...` 为主机生命周期事件；组件在**非实时函数**（create/prepare/destroy/set_parameter）中可 printf，输出被捕获进 UI 日志窗口；实时过程中的组件输出走 PROBE 轮询（每 200ms 上报 readback 参数），实时线程内禁止 printf/IO。
- **UI**：工具栏「⏺ 实时运行 / ■ 停止」；底部实时日志窗口；运行中修改非 `restart_required` 参数（如 gain_db）即时推送到 rt_host 生效；探针节点电平条每秒刷新。
- **关键修复**：设备回调周期可大于图 block_size（如 480 vs 128 帧）导致缓冲溢出崩溃——回调内按 block_size 分块处理；MinGW 管道输出需 setvbuf(_IONBF)+unitbuf；Python 侧用 readline() 而非迭代读子进程管道（迭代有预读缓冲）。
- **错误定位**：Runtime `load_plan` 的 prepare 失败会打印失败节点与组件（如 `[Runtime] prepare failed for node wav (orpheus.builtin.wav_in): -6`），实时/离线日志可直接定位到具体组件；-6 = ORPHEUS_ERR_NOT_FOUND（wav_in/mp3_in 多为文件路径不存在，路径相对工程目录）。

---

## 21. 两条执行路径（设计澄清）

- **动态加载（UI 运行所走）**：图编译只产出 plan.json 数据（拓扑、Buffer 分配、签名），不含任何代码生成；组件 DLL 预编译（缺了才补建）；基座程序（orpheus_runtime / orpheus_rt_host）LoadLibrary 动态加载，经 C ABI 函数表调用。图改动零 C 编译，编辑-运行循环快，面向 PC 设计/调试。
- **代码生成（部署路径）**：`orpheus-cli generate` 展开为独立 C 工程，静态编译，无 DLL 依赖，可交叉编译到嵌入式目标。目前仅支持单 Task、无探针。
- 两条路径共享同一份组件 C 源码与 ABI 契约，设计原则要求结果一致（自动化一致性测试待补）。

---

## 22. 运行方式统一与生成模式修复（已实现）

- **概念澄清**：运行方式只有两种——基座动态加载 / 代码生成后静态编译运行。WAV 还是系统音频是**输入输出组件**的事，可自由组合（如系统声音 → 处理 → WAV 录制）。
- **统一入口**：`POST /api/projects/{name}/run` 按图内容分流——含 device_in/device_out 的图进入实时会话（rt_host），纯文件图走离线宿主；UI 只有一个「▶ 运行」按钮。「⚙ 编译后运行」走 `run_generated`（生成独立 C 工程 → 静态构建 → 运行）。
- **rt_host 按图组合设备**：in+out+mic=duplex；in+out+loopback=环回+播放双设备；仅 out=播放时钟（WAV 播到声卡）；仅 in=采集/环回时钟（系统声音录到 WAV）。
- **生成器修复**：组件入口函数支持 `ORPHEUS_ENTRY_NAME` 宏（静态链接时各组件入口唯一，修复了之前所有节点共享第一个组件入口符号导致的崩溃——此前生成工程只验证过编译未验证运行）；生成参数表（类型化 OrpheusValue）传入 prepare；Buffer 指针按端口 ID 槽位绑定；ABI 头文件随工程复制（自包含，可脱离仓库编译）；main 支持 argv 指定块数。
- **一致性测试**：`test_generated_run_matches_dynamic_run` 对同一工程分别走动态/生成两条路径，逐字节比较输出 WAV（设计原则 5 的自动化落实）。

---

## 23. 时钟域与多速率模型（已实现 v1）

- **时钟源打标**：组件 manifest 声明 `clock_source: true` + `clock_domain`（device/file）。task 不显式建模——时钟源组件即时钟域的根。
- **编译期校验**：无时钟源的图走隐式宿主时钟（旧行为）；有时钟源时，任何不含时钟源的连通流报错（"算法流没有时钟驱动，无法启动"）；同一连通流混入两个强时钟域（非 file）报错并提示异步桥。
- **速率调整**：组件可声明 `scheduling.divisor: <expr>`——节点本身每块都跑，其输出域（及下游）每 N 块触发一次。表达式求值支持整数乘除链（`task:block_size*param:factor`、`task:sample_rate/param:factor`）。
- **新组件**：`downrate`（分频/重缓冲，超块 N×块长，速率不变，供控制速率算法）、`resample`（整数倍降采样 N:1，滑动平均抗混叠，输出速率=task/N）。
- **Runtime/生成器**：plan 每节点携带 `divisor` 与 `frames`（处理量子=上游 buffer 帧数）；执行时块计数相位触发（`(counter+1)%divisor==0`）。动态/生成两路径一致（逐字节一致性测试覆盖重采样链）。
- **时间树可视化**：编译响应携带每节点 `node_rates`（采样率/分频比/帧量子），UI 节点头部显示速率徽标（如 `24kHz ÷2`）。逻辑速率编译期可知；物理设备协商速率运行时由 rt_host 日志给出。
- 边界行为：块式抽取在输入块数为奇数倍时丢弃末尾未完成的输出块（≤1 个输出块）。
- 待做：async_bridge 组件（跨时钟域，rt_host 的 ma_pcm_rb 模式下沉）、timer 时钟源组件（控制周期任务）、升采样。

## 24. 组件自定义 UI 与波形回读（已实现 v1）

- **PROBE_JSON 数据通路**：宿主对 STRING 型 readback 参数输出 `PROBE_JSON <node> <param> <json>`（整行 JSON，数组/对象/数字），`rt.py` / `app.py` 解析为结构化值；旧 `PROBE <node> <param> <value>` 标量格式完全兼容。设计文档：`docs/design_component_ui.md`。
- **probe_waveform 波形显示**：组件内置 1024 帧环形缓冲（取第 0 通道），`waveform` readback 参数在非实时线程编码为 JSON 数组；画布节点注册 `ScopeWidget`（canvas 示波器，消费 `data.probe.waveform`），离线与实时会话均显示。示例：`examples/probe_waveform_scope.yaml`。
- **显示型 readback 参数**：参数面板隐藏 `readback && !affects_signature` 的参数（如 rms/peak/waveform），它们是探针输出而非可编辑输入。
- **机制原则**：UI 定制 = manifest 软声明（可选）+ 前端注册表（`widgets.js` 参数控件 / `nodeWidgets.js` 节点本体），C ABI、plan、编译、Runtime、代码生成完全不感知；无注册时回退默认渲染。

## 25. MP3 输入组件（已实现 v1）

- **`orpheus.builtin.mp3_in`**：基于 vendored miniaudio 的 `ma_decoder`（内嵌 dr_mp3 0.7.3）解码 MP3；prepare 时整文件解码为 f32（按图采样率/通道数重采样，与 wav_in 的读取语义一致），`total_frames` readback 供离线宿主确定时长。manifest 声明 `deps: [miniaudio]`。
- **文件控件扩展**：参数级 `file_ext`（如 `.mp3`）控制文件浏览/上传的扩展名过滤（`widgets.js` FileWidget），默认仍为 `.wav`。
- **代码生成**：生成器改为按 manifest `sources` 列表编译组件（支持多源文件），并复制 `third_party/miniaudio.h` 到生成工程（自包含，可脱离仓库编译）；修复生成 main 不调用 `destroy` 导致 wav_out 不落盘的问题（此前一致性测试空洞通过——比较的是动态运行留下的同一文件）。
- 示例：`examples/mp3_play.yaml`（MP3 → Gain → WAV），测试素材 `examples/test_input.mp3`（ffmpeg 生成的 2s 440Hz 正弦）。
- **Windows 中文文件名**：wav_in / mp3_in 的文件路径是 UTF-8，而 Windows 窄 `fopen` 按 ANSI 代码页解释，中文/特殊字符文件名会打不开（prepare 返回 -6）。已改用宽字符 API（`_wfopen` / `ma_decoder_init_file_w`）打开。

## 26. 输出 fan-out、监控增强、FIR/扫频/频谱、子组件框选（已实现 v1）

- **fan-out 修复**：源端口连接多个下游时，此前只有最后一条连接收到数据（每个连接一个 buffer、输出槽后写覆盖）。运行时与代码生成器均改为「同一源端口共享同一 buffer」，所有下游读到完整数据；e2e 测试验证两路 RMS 一致、输出 WAV 逐字节相同。
- **监控界面增强**：电平条/示波器大屏化（`large` 模式 + 放大弹层 ⤢）；示波器改滚动历史显示（250ms 轮询）。
- **`orpheus.builtin.fir`**：系数以逗号分隔字符串传入，每通道环形延迟线，实时安全；数值测试与 numpy 卷积逐点比对。
- **`orpheus.builtin.sweep_gen`**：对数/线性扫频发生器（起止频率、时长、幅度）；配合 `probe_waveform`（示波）、`wav_out`（记录）和 `probe_spectrum`（绘图）形成完整扫频测量链路，示例 `examples/sweep_spectrum.yaml`。
- **`orpheus.builtin.probe_spectrum`**：radix-2 FFT + Hann 窗，`spectrum` readback 以 PROBE_JSON 输出幅度数组；UI 频谱控件按采样率/窗口绘制 dB 柱状图，可放大。
- **子组件框选**：React Flow 开启 `selectionOnDrag`，画布拖拽框选 + Shift 点击多选；「包装为子组件」自动推导边界端口并打开新标签页（多标签已支持），层级嵌套由 `flatten_project` 递归展开。

---

## 27. 数据 ID、模块内存与内存透明（已实现）

> 详细设计：`docs/design_registry.md` §17。目标：统一寻址（调音/实时控制/探针/状态）、模块内存连续、
> 内存透明（ID → 类型/长度/地址可查询），对齐公司模型习惯（RTC/TOP/TSP 三类 ID，但按我们自己的
> 用途/形式正交模型组织，且不拆读写）。

### 27.1 32 位数据 ID（单 ID，方向只在接口）

- 布局：`bits31..28` 用途（purpose），`bits23..16` 模块 id，`bits15..0` 模块内槽序号。
- 用途按使用频率排序：`0x0 RTC`（实时控制：音量/fade/balance 等实时参数 + 命令 + 实时信号输入——
  用户界面调，MCU 用该 ID 写 DSP）/ `0x1 TUNE`（调音/配置，常为 bulk 包）/ `0x2 PROBE`（观测回读）/
  `0x3 STATE`（调试状态）/ `0x4 CUSTOM`（用户自定义）/ `0x5..0xF Reserved`。
- 形式（`OrpheusDataForm`：SCALAR / BULK / MODULE）与用途正交，不进 ID 位，由 ID map 的
  `form/count/byte_size`（`ORPHEUS_CHAR_COUNT_*`）描述——「TUNE 又是 bulk 形式」因此不矛盾。
- 命名：`ORPHEUS_<KIND>_<模块Camel><参数Camel>`（单叶子模块=模块+参数，多叶子带叶子名防冲突）。

### 27.2 模块内存连续（flatten 与布局正交）

- `plan.modules`：按节点 id 的 `__` 路径前缀组织模块树，DFS 分配稳定模块 id；模块内叶子槽按执行序。
- 生成路径：按子组件实例生成**嵌套结构体**（`include/orpheus_arena.h`，`OrpheusMod_*` + `OrpheusArena`），
  每个子模块实例一块连续内存，布局由 C 编译器决定。
- 动态路径：Runtime 按 `plan.modules` **切片分配**——每个模块（含根）一块连续内存，
  叶子 `state_block = 模块基址 + 叶子偏移`，与生成路径同一规则；模块包 ID（槽号 `ORPHEUS_ID_SLOT_MODULE`）
  因此有真实基址。

### 27.3 内存透明（resolve / map）

- `plan.id_map` 是唯一 ID 表（编译器按模块/槽/用途/形式/类型/个数生成），动态 Runtime 与代码生成共用。
- `Runtime::resolve(id, OrpheusResolvedData*)`：数据点返回真实地址（base = 状态块 + 槽偏移）、
  类型、长度、模块、槽、节点、key、中文名；模块包返回整块基址与字节数。
- 入口：离线宿主 `orpheus_runtime --resolve <id> | --map`；rt_host stdin `RESOLVE <id>` / `MAP`；
  后端 `GET /rt/resolve?id=` / `GET /rt/map`；UI 参数面板显示 0x ID 并可解析地址。
- 生成路径产物：`orpheus_ids.h`（宏 + CHAR_COUNT）、`src/orpheus_id_map.c`（offsetof/sizeof 精确偏移）、
  `memory_map.md`（可读布局）。

### 27.4 按 ID 的实时控制（RTC 通道）

- rt_host stdin：`RW <id> <value>`（按 ID 写，非 PROBE/STATE）、`RR <id>`（读回 `RVALUE`）、
  `RWB <id> <n> <v0>...`（bulk 直写）、`GETBULK <node> <key>` / `RGB <id>`（bulk 读回 `BULKVALUE`）；
  后端 `POST /rt/write`、`/rt/read`、`/rt/write_bulk`、`/rt/read_bulk`。
- 方向只在接口 + 注册表按用途强制方向（PROBE/STATE 拒写、命令拒读），不靠 ID 拆位。

### 27.5 BULK 双 bank（Runtime 层，glitch-free）

- 组件只注册一块 BULK active 区，**双 bank 可选**：组件声明语义意图
  （`ORPHEUS_SLOT_DOUBLE_BUFFERED` + manifest `bulk_slots[].double_bank`），
  工程级 `double_bank: auto|on|off` 决定部署生效（默认 auto；off=直写即时生效、省内存）。
- 生效槽：Runtime 分配**影子区**：
  `write_bulk` 越界检查后仅 memcpy 进影子（标记 pending），`process_block` 块边界一次性
  memcpy 提交到 active——组件零样板，并发/原子性由框架承担（仓库原则）。
- **生成路径同样支持**（部署到 MCU 才有意义）：生效槽在生成代码里产出 `orpheus_control.c`
  （影子数组 + 槽表 + 按 node/key 与按 ID 的 bulk 读写 + 块边界提交），
  生成 main 带 `--write-bulk/--read-bulk/--run` 控制 CLI；off 时零影子直写。
- `get_bulk`：读 active bank，越界检查后仅 memcpy（高速大块特性）。
- UI：工程设置一个下拉（自动/全部/关闭）；参数面板 bulk 行只显示只读「双缓冲」徽标，无逐行开关。
- **实际场景定位**：双 bank 是少数派——常规无毛刺调音惯例是 mute → 更新系数 → unmute
  （mute 是 RTC 实时参数）；双 bank 仅用于必须边跑边更的少数系数。

---

## 28. 二进制消息协议（CALL / RESPONSE / NOTIFICATION）

> 设计：`docs/design_registry.md` §18。统一语义：kind = 运行时确定语义，hook = 扩展缝（外部注册优先），
> CUSTOM = 用户完全自处理的消息。

- **Response = 同步返回**：所有 CALL 都同步得到一个 RESPONSE；**Notification = 异步交付/事件推送**，
  无返回；异步操作 = CALL 先 RESPONSE（受理）→ 结果经 NOTIFICATION（带 call_id）送达。
- 信封：8 字节头（route_id 占满一个 uint32；另一个字 = msg_type 2b + flags 4b + call_id 16b +
  payload_words 10b），payload 4 字节对齐，消息自描述，无帧长前缀；小端。
- 分发优先级：外部注册 hook → 组件接口 hook → 默认槽语义（确定性 kind 读写；CUSTOM 必须由 hook 处理）。
- 入口：`Runtime::register_hook/message`、rt_host `MSG <hex>`、离线 `--msg <hex>`、
  后端 `POST /rt/msg`（按 call_id 匹配响应）；生成侧 `orpheus_control_message/register_hook` 同款。
