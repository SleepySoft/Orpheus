# Orpheus 当前状态与路线图

> 本文只记录当前状态和下一阶段；历史过程见 `implementation_log.md`，产品目标见 `WHAT.md`。

## 已完成基线

- YAML 图、组件 Registry、编译器、C/C++ Runtime、React Flow 编辑器。
- 文件与设备音频、实时参数和探针、动态加载与独立 C 工程生成。
- ABI v3、统一 arena、32 位数据 ID、BULK 双 Bank、消息协议。
- 子组件递归展开、目标平台/alter、OLINK/串口会话、控制参数链路。
- 多速率静态调度与 `rate_sync` 合流；动态和生成路径一致性测试。
- 72 个内置组件，均有组件 README。

## P0 工程基线

- [x] `cli build` 构建组件、runtime、ABI smoke test 和 OLINK CLI。
- [x] Fresh CMake 构建优先使用可用 MSVC 环境，不依赖 PATH 猜测编译器。
- [x] Windows CI 固定 Python 3.12、Node 20 和 MSVC。
- [x] `scripts/verify.ps1` 统一执行安装、构建、CTest、pytest、Jest 和 UI 构建。
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
