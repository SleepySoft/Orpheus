# Orpheus 基础版本实施日志

## 2026-08-05（晚）

### 六项批量任务（各自独立提交）

- `35898ec` fix: 输出 fan-out——同源端口共享 buffer，多下游全部收到数据（运行时 + 代码生成器）。
- `71e85ee` ui: 监控控件增强（电平条/示波器大屏化 + ⤢ 放大弹层）。
- `4b8a598` feat: FIR 滤波器组件（系数字符串 + 环形延迟线 + numpy 卷积数值测试）。
- `5c40d36` feat: 频谱分析组件（radix-2 FFT + Hann 窗 + canvas 频谱控件）。
- `336e98d` feat: 扫频发生器（对数/线性）+ 扫频-示波-记录-绘图示例与 e2e。
- `28dfbcb` ui: 子组件框选（`selectionOnDrag`），包装流程可用（多标签/层级原有实现）。

## 2026-08-05

### probe_waveform 波形显示 + 组件自定义 UI v1（commit 4f73237）

- 修复 `probe_waveform` 无显示：组件内置 1024 帧环形缓冲 + `waveform` readback（非实时线程编码 JSON 数组）；宿主对 STRING readback 输出 `PROBE_JSON <node> <param> <json>`（旧 `PROBE` 标量格式兼容）；后端 `rt.py`/`app.py` 解析结构化探针值；UI 注册 `ScopeWidget`（canvas 示波器），参数面板隐藏显示型 readback 参数。
- 设计文档 `docs/design_component_ui.md`（可选、不耦合、注册表驱动的控件机制）。
- MSVC 构建支持：顶层 CMake 加 `/utf-8 /EHsc`、Windows 统一 DLL `lib` 前缀。

### MP3 输入组件（待提交）

- 新增 `orpheus.builtin.mp3_in`：miniaudio `ma_decoder`（dr_mp3）解码，prepare 整文件转 f32（按图速率重采样），`total_frames` readback；manifest `deps: [miniaudio]` + 参数 `file_ext: .mp3`。
- 代码生成：按 manifest `sources` 编译组件、复制 miniaudio.h 保证生成工程自包含；修复生成 main 缺 destroy 导致 wav_out 不落盘（一致性测试此前空洞通过）。
- 示例 `examples/mp3_play.yaml` + 素材 `examples/test_input.mp3` + e2e 测试（上传 mp3 → 离线运行 → 输出 WAV）。

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


---

## 2026-08-05：从 references/FAW_E202_DEMO 提取车载音频算法为组件

### 背景
`references/FAW_E202_DEMO` 是 Bose AudioWeaver 的 FAW（一汽）E202 车载音频示例工程（C/C++ + MATLAB 代码生成）。其娱乐（ENT）信号链 `Mute -> Gain -> Bass -> Treble -> MidRange -> Fade` 等模块在 `Inner/*_Process.c` 与 `Source/Mod*DemoModule.c` 中实现了真实 DSP 算法（增益斜坡、一阶/二阶 IIR 搁架、频谱分频淡入淡出、矩阵路由）。本迭代把这些算法“正确做成” Orpheus 组件。

### 组件清单（8 个，均为 source 包）

**基础 BIG6（音效算法类，新增分类 `音效算法`）**——ENT 链 6 个核心音效算法：
| 组件 id | 名称 | 算法来源 | 要点 |
|---|---|---|---|
| `orpheus.builtin.mute` | 静音 | MuteDemo | 增益斜坡软静音（0=正常 1=静音），消除咔哒声（参考版是硬开关，此处改进为斜坡） |
| `orpheus.builtin.bass` | 低音 | BassDemo / Tone1 | 一阶低频搁架：`out = in + boost*LPF(in)`，`boost=10^(gain_db/20)-1` 斜坡 |
| `orpheus.builtin.treble` | 高音 | TrebleDemo / Tone1 | 一阶高频搁架：`out = in + boost*HPF(in)`，HPF=in-LPF |
| `orpheus.builtin.midrange` | 中音 | MidRangeDemo | 二阶带通搁架（Direct Form II Transposed，b2=-b0）+ 增益斜坡 |
| `orpheus.builtin.fade` | 前后衰减 | FadeDemo | 频谱分频淡入淡出：低频全通、高频按前/后增益斜坡衰减；`out=LPF+gain*(in-LPF)` |
| `orpheus.builtin.balance` | 左右平衡 | BalanceDemo | 左右平衡增益斜坡（偶通道=左、奇通道=右） |

**其它音频算法（通道路由类）**：
| 组件 id | 名称 | 算法来源 | 要点 |
|---|---|---|---|
| `orpheus.builtin.input_select` | 输入选择 | InputSelectDemo | 每路输出选一路输入（`select` 逗号分隔 1 起索引，0=静音），输入/输出通道数独立 |
| `orpheus.builtin.output_router` | 输出路由 | OutputRouterDemo | 矩阵混音（`matrix` 行优先逗号分隔增益，`identity`=对角直通），支持任意 in->out 通道映射 |

> 说明：`gain` 已有组件（含平滑），故 BIG6 中不再重复建 gain。Bass/Treble 的实际算法在 `Source/ModBassDemoModule.c`/`ModTrebleDemoModule.c`（与 `InnerTone1_Process.c` 同构的并行搁架+斜坡）。

### 分类与 UI
- 新增 Palette 分类 **`音效算法`**（介于 `基础算法` 与 `通道路由` 之间）：`ui/src/Palette.js` 的 `CATEGORY_ORDER` 已加入。
- `SKILL/references/write-component.md` 的 category 注释列表已同步加入 `音效算法`。
- 路由两件归入既有 `通道路由`。

### 实现要点（踩坑）
- **编码红线**：组件 `.c` 一律英文注释、纯 ASCII；中文只在 `component.yaml`（name/category/description）。写文件必须用 `[System.IO.File]::WriteAllText(.., UTF8 no BOM)`——**禁止** PowerShell 管道 `here-string | python`（`$OutputEncoding` 默认 ASCII 会把中文毁成 `?`），也禁止 `Set-Content`（GBK）。
- **参数类型与运行时传参**：rt_host `SET` 与 plan->prepare 对非 `channels`/`sample_rate` 的数值参数一律按 FLOAT 传；故所有可实时调参项用 `float`（含 mute 0/1、fade/balance -1..1），路由 `select`/`matrix` 用 `string`（restart_required，prepare 解析）。`set_parameter` 同时容忍 FLOAT/INT。
- **非对称通道**：`input_select`/`output_router` 用 `channels_in`/`channels_out` 两个 affects_signature 参数，端口 `channels: param:channels_in`/`param:channels_out`；process 中直接用 `in->channels`/`out->channels`（运行时按端口签名分配，恒正确），prepare 从 param_values 读（容忍 FLOAT/INT）。
- output_router 矩阵以固定 stride `MAX_CH(32)` 存储（`matrix[o*MAX_CH + i]`），prepare 与 process 一致。

### 验证
- `cli scan` 发现全部 8 个；`cli build` 编译通过（8 个 DLL）；`pytest test_server.py::test_components_have_chinese_name_and_category`、`test_variable_ports.py` 全绿。
- 端到端离线运行（`examples/smoke_big6.yaml`：sig->mute->bass->treble->midrange->balance->rms->wav_out，48k×480k 帧，RMS 0.443，WAV 写出）；`smoke_fade.yaml`（4ch fade，RMS 0.313）；`smoke_routing.yaml`（output_router 2->6 上混 + input_select 6->2，RMS 0.345）均跑通。

### 已知环境问题（非本迭代引入）
- 若 `orpheus_rt_host.exe` 在运行，会锁定已加载组件 DLL，导致 `cli build` 对该 DLL 报 `Permission denied`（如 biquad）。停掉实时会话后即可全量 build。新组件 DLL 不受影响。

### 下一步建议
- 为 8 个组件补 Golden Vector / 单元测试（目前仅端到端冒烟）。
- UI `nodeWidgets.js` 可为 bass/treble/midrange/fade/balance 加频响示意或电平条（可选）。
- 考虑把 `input_select`/`output_router` 的 `select`/`matrix` 做成可视化矩阵编辑控件（当前是 text）。
