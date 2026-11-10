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
- alter 连通组在画布自动绘制「N 选 1」框，平台专属节点以有文字的彩色徽标和边框区分；不改变工程格式或解析语义。
- Bridge 主机核心：半双工单请求基线、全双工流水化能力、可替换 Transport/Codec，以及异步文件日志 Sink。
- HLOS Transport：stdio/process 双管道、TCP、Windows Named Pipe/POSIX Unix Socket、长度前缀 Codec 与可替换 Adapter 注册表。
- 75 个内置组件，均有组件 README。

## 当前验证基线

- pytest：266 passed，1 skipped（可选演示组件未安装）。
- CTest：7/7（ABI、loader、绝对时间线、RNC MIMO NLMS、外部参考模型 SoftClipper、节目响度均衡及其控制原子）。
- 前端：Jest 17/17、生产构建、Playwright 2/2 核心流程通过。
- 外部参考模型：ASM 48-target、SAS 68-target 独立生成工程构建成功；ASM 全局及关键 Task 入口运行通过。

## P0 工程基线

- [x] `cli build` 构建组件、runtime、ABI smoke test 和 OLINK CLI。
- [x] Fresh CMake 构建优先使用可用 MSVC 环境，不依赖 PATH 猜测编译器。
- [x] Windows CI 固定 Python 3.12、Node 20 和 MSVC。
- [x] `scripts/verify.ps1` 统一执行安装、构建、CTest、pytest、Jest、UI 构建和 Playwright。
- [x] 当前状态文档成为待办的唯一入口。

## P1 框架完善

### 处理调用 Wrapper 与调用统计树（设计记录，暂不实现）

2026-11-10：[讨论细化与案例推演](reference/process-wrapper-design.md) 已记录。
范围是 Task 驱动链上的全部组件 `process`，不包括生命周期、参数或通信接口；
Task 建立共享上下文，Runtime 和生成 C 复用统一处理边界，不改变组件自主续调。

- [ ] 公共 wrapper 与上下文传递；区分进入、输出发布后且下游调用前、
  整次嵌套调用结束三个阶段，覆盖原地 Buffer、重复续调与错误收尾。
- [ ] 固定容量、按 Task/调用路径聚合的统计树；明确采样、自身/包含耗时、
  截断状态和内存预算，不保存逐调用日志。
- [ ] 可裁剪的线程安全快照导出；实时路径不等待外部读取。
- [ ] 按构建能力、组件能力、实例策略配置；质量检测与专用 bypass 分阶段实现，
  不支持的能力显式拒绝，不自动复制端口或补调下游。
- [ ] 验证动态/生成语义一致性、功能全关结果不变，以及增量实时开销。

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

### 编辑事务与 undo/redo（需求记录，暂不实现）

2026-11-10：先记录需求，本次不启动功能实现或全面 Command 化。
建议分阶段推进：**尽早统一编辑状态与修改事务入口，撤销重做功能可在核心编辑行为
较稳定后实现**，不必等待框架所有功能完善。后续新增编辑操作时避免继续增加分散的
状态修改入口，以免扩大迁移成本；基础整理也应作为独立任务安排，不混入其他功能修改。

- [ ] 统一工程设计状态和事务边界：节点、连线、参数、子组件定义与公开接口、
  Task/工程设置、预设及布局；一次用户操作涉及的关联修改原子提交。
  选区、视图切换、端口解析缓存、探针和运行日志等不纳入设计撤销历史。
- [ ] 基于事务记录状态历史：可先用快照验证正确性，再按大工程内存开销决定是否
  改为正向/逆向结构补丁。补丁是工程数据的差异，不是 YAML 文本 diff。
  删除节点须一并恢复关联连线等数据；不要求为每类操作手写逆命令。
  Command 可以表达操作意图，但不作为实现 undo/redo 的前置条件。
- [ ] 明确操作分组：一次拖拽、一次连续参数输入合并为一次撤销；
  包装子组件等批量操作只产生一个历史项。撤销后发生新编辑时清空 redo 分支。
- [ ] 自动保存持久化当前恢复后的设计状态，不把保存动作当成历史项；
  切换工程、重新加载或接管刷新后建立新基线，失去编辑权时禁止 undo/redo。
- [ ] 明确副作用边界：笔记是否拥有独立历史另行决定；
  实时设备写入、运行/停止、文件上传及直接文件删除不默认随工程设计撤销。
- [ ] 补齐复杂操作往返、连续操作合并、自动保存、编辑权失效、
  历史容量及大工程内存开销测试。

粗估而非工期承诺：仅主图基本操作约 2–4 个开发日；覆盖全部工程编辑入口与回归
约 1–2 周，正式排期前需逐入口拆分。完整手写 Command 逆操作通常成本更高。

## P2 候选

- [x] 子组件公开参数、实例参数提升与跨子图控制链。
- [x] 外部参考模型 RNC 12×8×125 MIMO NLMS 核心、12000 权值提取器与 golden。
- [x] 外部参考模型 SAS 二次分段 SoftClipper 与 参考工程 B TOP 参数回填。
- [x] 参考工程 A 历史跨 Task 边迁移到 `async_bridge`，生成工程可构建运行。
- [x] 课程包步骤、结构化自动检查 API 与条件化教学面板。
- [ ] RNC 200-tap Wiener filtered-error、系数历史与发散恢复状态机。
- [ ] EHC 谐波参考/FxLMS 与 SAS FDP 双速率 STFT。
- [ ] 含代码的封装型复合组件库与动态数量运行槽。
- [ ] 教师答案/进度持久化和 Tauri 桌面封装。

## P3 观测点与 Adapter

- [x] 图本体与宿主分离，实时调用链无 printf/文件/串口 IO；结构化错误由外部处理。
- [x] 顶层 `bridges` + 无端口「访问桥」配置节点；`uart_link` 自动迁移，当前 `uart + olink` 经 SerialSession 贯通。
- [x] Access Bridge 分层定案：Backend / Endpoint / Codec / Transport / BridgeSession，明确与音频 `async_bridge` 无关。
- [x] Python `BridgeSession` 核心、`ByteTransport`/`FrameCodec` 接口和 `OlinkCodec`；串口迁移为薄组合，半/全双工并发行为有自动化测试。
- [x] 异步文件 Log Sink；当前动态/生成、长驻/一次性及串口会话日志统一归档到工程 `logs/`，实时线程不执行文件 IO。
- [x] HLOS stdio/process、TCP 和本地 Pipe Adapter；TCP/Pipe 可经 REST 连接已有 Endpoint，TCP 半双工/全双工与跨平台本地 Pipe 有回环测试。
- [x] RuntimeBackend + GeneratedBackend 共用 §18 dispatch；rt_host/host_win/host_cli 接入 LengthPrefix 二进制 Endpoint，删除文本协议。
- [x] HELLO/IDENTITY、能力位、graph/plan/id_map hash、STOP/STATS/分页 MAP 与 hash 错配写保护。
- [ ] 同 call_id 幂等响应缓存与 BULK 分片。
- [x] 本机动态/生成宿主接入二进制 Endpoint，主动推进宿主会话化，服务关闭/工程删除统一回收。
- [ ] 全双工主动通知/请求流水化与 Observation credit；半双工继续作为所有 Adapter 的降级基线。
- [ ] SoC/HLOS 的 RPMsg、SHM 零拷贝、多 Lane、TLS/认证、lease 与权限。
- [ ] 工程顶层 `observations`、稳定观测 ID 与只读音频 Buffer view API。
- [ ] 本地 Runtime Adapter 与 UI 观察端点编辑，传输层对 UI 透明。
- [ ] `observation_uart` / shared-memory / callback Adapter，固定容量快照、限流与丢弃计数。
- [ ] 纯观测 probe 迁移 pass：`observability: none|metadata|embedded`；参与控制链的观测计算禁止裁剪。

## P4 时间线模型

- [x] ADSP-21593 SPORT Boundary V1：Clock Domain / Trigger Group / Task 绑定、slot/格式静态映射、外部 epoch/frame/sequence Task 入口与违约统计；EREV CCES 壳集成构建通过。
- [x] P0a：ABI v4 增加 `frame_index/epoch/valid_frames/timeline_flags`；动态/生成路径填写节点本地绝对帧和派生 timestamp，Task 独立推进，首次触发标记 discontinuity。
- [ ] P0b：reset/seek/restart 递增 epoch；动态/生成通过可观测测试组件逐字段一致性验证。
- [ ] P0c：plan 显式记录外部节拍/主动推进触发，UI 只在 active 下显示 pacing。
- [ ] P1：source EOS/valid_frames 正式传播；汇总 `latency_samples`，生成 source-to-sink 延迟报告与合流补偿策略。
- [ ] P2：Task 独立时间线、明确 clock master、有理数速率映射；异步桥增加时间戳、sequence、drift ppm 与 ASRC/PLL 策略。
- [ ] P3：参数事件支持 target frame/sample offset；Observation 统一携带 timeline/epoch/frame/sequence/dropped，支持确定性录制重放。
