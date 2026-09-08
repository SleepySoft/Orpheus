# Orpheus 当前状态与路线图

> 本文只记录当前状态和下一阶段；历史过程见 `implementation_log.md`，产品目标见 `WHAT.md`。

## 已完成基线

- YAML 图、组件 Registry、编译器、C/C++ Runtime、React Flow 编辑器。
- 文件与设备音频、实时参数和探针、动态加载与独立 C 工程生成。
- ABI v4（处理上下文含绝对帧时间）、统一 arena、32 位数据 ID、BULK 双 Bank、消息协议。
- 子组件递归展开、目标平台/alter、OLINK/串口会话、控制参数链路。
- 多速率静态调度与 `rate_sync` 合流；动态和生成路径一致性测试。
- 生成图本体 `orpheus_graph` 静态库与宿主解耦；最小 main、PC CLI、Windows 宿主各自独立。
- 运行三轴层级术语定案：执行实现、执行触发、访问端点为顶层维度；全速/按现实时间归入主动推进 pacing；图时间线独立建模，“离线运行”迁移为“无设备批处理”。
- 73 个内置组件，均有组件 README。

## 当前验证基线

- pytest：244 passed，1 skipped（可选演示组件未安装）。
- CTest：5/5（ABI、loader、绝对时间线、RNC MIMO NLMS、BAF SoftClipper）。
- 前端：Jest 15/15、生产构建、Playwright 核心流程通过。
- BAF：ASM 48-target、SAS 68-target 独立生成工程构建成功；ASM 全局及关键 Task 入口运行通过。

## P0 工程基线

- [x] `cli build` 构建组件、runtime、ABI smoke test 和 OLINK CLI。
- [x] Fresh CMake 构建优先使用可用 MSVC 环境，不依赖 PATH 猜测编译器。
- [x] Windows CI 固定 Python 3.12、Node 20 和 MSVC。
- [x] `scripts/verify.ps1` 统一执行安装、构建、CTest、pytest、Jest、UI 构建和 Playwright。
- [x] 当前状态文档成为待办的唯一入口。

## P1 框架完善

### 多 Task 与异步桥

- [x] Plan 显式记录 Task 列表、节点归属、各 Task 的入口与周期。
- [x] Runtime 与生成工程提供等价的 per-Task process 入口（当前由宿主串行调度）。
- [x] `async_bridge` 使用固定容量 SPSC Ring Buffer 连接不同 Task。
- [x] 增加欠载、溢出和水位探针及 Ring Buffer 回绕一致性测试。

### 质量覆盖

- [x] 前端图文档测试覆盖 Task 归属、控制链与音频边往返。
- [x] C/C++ 严格警告、ASan/UBSan 构建开关和动态库加载错误路径测试。
- [x] 128 节点编译/运行及块级 p50/p95/p99/max 延迟基准脚本与 CI 记录。
- [x] Playwright 覆盖 Task 配置、节点归属与后端持久化流程。
- [ ] 真实声卡端到端延迟基线（需要固定音频硬件回环测试台）。

### 跨平台

- [x] Linux GCC、Linux Clang+Sanitizer 与 macOS Clang CI 矩阵。
- [ ] 首次远端 CI 绿灯后固化平台差异修复。
- [ ] 将 `win`/`dsp` 扩展为数据驱动的 Target Profile 能力检查。

## P2 候选

- [x] 子组件公开参数、实例参数提升与跨子图控制链。
- [x] BAF RNC 12×8×125 MIMO NLMS 核心、12000 权值提取器与 golden。
- [x] BAF SAS 二次分段 SoftClipper 与 EREV-1 TOP 参数回填。
- [x] Symphony ASM 历史跨 Task 边迁移到 `async_bridge`，生成工程可构建运行。
- [x] 课程包步骤、结构化自动检查 API 与条件化教学面板。
- [ ] RNC 200-tap Wiener filtered-error、系数历史与发散恢复状态机。
- [ ] EHC 谐波参考/FxLMS 与 SAS FDP 双速率 STFT。
- [ ] 含代码的封装型复合组件库与动态数量运行槽。
- [ ] 教师答案/进度持久化和 Tauri 桌面封装。

## P3 观测点与 Adapter

- [x] 图本体与宿主分离，实时调用链无 printf/文件/串口 IO；结构化错误由外部处理。
- [x] 顶层 `bridges` + 无端口「访问桥」配置节点；`uart_link` 自动迁移，当前 `uart + olink` 经 SerialSession 贯通。
- [x] Access Bridge 分层定案：Backend / Endpoint / Codec / Transport / BridgeSession，明确与音频 `async_bridge` 无关。
- [ ] RuntimeBackend + GeneratedBackend 共用 §18 dispatch；rt_host 增加二进制 PipeTransport，文本协议降为兼容入口。
- [ ] BridgeSession 统一 RtSession/SerialSession，增加 HELLO、能力位、plan/id_map hash 与订阅服务。
- [ ] 工程顶层 `observations`、稳定观测 ID 与只读音频 Buffer view API。
- [ ] 本地 Runtime Adapter 与 UI 观察端点编辑，传输层对 UI 透明。
- [ ] `observation_uart` / shared-memory / callback Adapter，固定容量快照、限流与丢弃计数。
- [ ] 纯观测 probe 迁移 pass：`observability: none|metadata|embedded`；参与控制链的观测计算禁止裁剪。

## P4 时间线模型

- [x] P0a：ABI v4 增加 `frame_index/epoch/valid_frames/timeline_flags`；动态/生成路径填写节点本地绝对帧和派生 timestamp，Task 独立推进，首次触发标记 discontinuity。
- [ ] P0b：reset/seek/restart 递增 epoch；动态/生成通过可观测测试组件逐字段一致性验证。
- [ ] P0c：plan 显式记录外部节拍/主动推进触发，UI 只在 active 下显示 pacing。
- [ ] P1：source EOS/valid_frames 正式传播；汇总 `latency_samples`，生成 source-to-sink 延迟报告与合流补偿策略。
- [ ] P2：Task 独立时间线、明确 clock master、有理数速率映射；异步桥增加时间戳、sequence、drift ppm 与 ASRC/PLL 策略。
- [ ] P3：参数事件支持 target frame/sample offset；Observation 统一携带 timeline/epoch/frame/sequence/dropped，支持确定性录制重放。
