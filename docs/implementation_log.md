# Orpheus 基础版本实施日志

## 2026-08-10（第四十三次讨论：BAF SAS step0 骨架 + per-channel IIR/delay_line）

- **iir_bank 支持 per-channel 系数**
  - `component.yaml` 新增 `coefs_mode: shared | per_channel` 与 `coefs_per_channel` BULK 槽。
  - `iir_bank.c`：per-channel 模式下按「通道 → 级 → 5 元组」解析字符串/BULK，
    写入每通道独立的 `coefs[cc][5*i+k]` 槽位（保留 `MAX_STAGES` 填充，与 BULK 内存布局一致）。
  - 新增 `test_per_channel_components.py`：验证 per-channel 增益不同、shared 向后兼容。
- **delay_line 多通道独立延迟线**
  - 补齐 `CMakeLists.txt`；修正 C 描述符中不存在的 widget/kind 常量。
  - 缓冲区容量改为 `max_delay + block_size + 1`；`prepare` 解析 `delays_samples` 字符串默认值。
  - 新增测试验证每通道不同延迟。
- **BAF PostProcess 骨架**
  - 新增 `examples/baf_postprocess.yaml`：32ch -> 22ch 主输出，覆盖
    input_select/gain_ramper/limiter/iir_bank/soft_clipper/output_router/delay_line/probe/wav_out。
  - 离线运行通过，主输出 RMS > 0.01。
- **BAF SAS step0 完整骨架**
  - 新增 `examples/baf_sas_step0.yaml`：Medusa 链 -> 1 块延迟反馈 -> MusicIn ->
    InputSelect/PreAmp/PostProcess/Audiopilot，使用子组件层级结构。
  - 子组件占位实现：`medusa_chain`、`input_select`、`pre_amp`、`post_process`、`audiopilot`，
    内部用 input_select/output_router/mixer/gain/delay_line 保持通道数转换。
  - 新增 `test_baf_step0.py`：编译/运行 end-to-end 验证；全量测试 117 passed、1 skipped。

## 2026-08-10（第四十二次讨论：FDP 架构修正 + slc_matrix_mul 接入 + UI 参数面板）

- **FDP 架构修正（核心变更）**：源码实证发现 FDP（TID2）不是分析侧链，而是主链内联多速率组件。
  `step0`（TID0, 1500Hz）第一个调用即 `MedusaPart2FdpFullRateTID0()`（:15600），紧跟 `MedusaPart3FullRateMixing()`（:15606），
  FDP 6ch 输出直接喂 Part3。TID2（375Hz）只是 FFT 核心降速率处理，非独立分析抽头。
  双速率机制：TID0 写/读 32 样本/块的 256 样本环形缓冲，TID2 每 4 块读 128 样本做 256 点 STFT（50% 重叠，hop=128）。
  - `baf_sas_full.yaml`：TID0 chains 加入 `part2_fdp`（Part3 之前）；TID2 改 `mode: inline`、`chains: []`、加双速率 note。
  - `distill_topology.py`：新增 `mode == "inline"` 判断（:258），inline 的 task chains 并入主链，不生成死路抽头。
  - 测试断言更新：节点数 25->23、downrate 5->4（无 TID2）、part2_fdp 在主链、tap_2 不在主链。
- **slc_matrix_mul 接入**：Part3 混音矩阵从 flow 注释 `MixingMatrix` 替换为 `SlcMatrixMul` 组件映射；
  `distill_topology.py` `_RULES` 新增 `slcmatrixmul -> slc_matrix_mul`（:33）。
- **"分析汇"改名**：`distill_topology.py` 中 `embed_out` 死路终点 label 从"分析汇 TIDn"改为"分析抽头终点 TIDn"（3 处）；
  全仓库扫描替换残留引用（`baf_sas_full.yaml`、`baf_sas_analysis.md`、`implementation_log.md`、`SKILL/references/distill-model.md`）。
- **SKILL/references/distill-model.md 更新**：§3 映射表加 `slc_matrix_mul` 行；§4.1 示例 FDP 改 `mode: inline`；
  规则加 inline 模式说明（区别于分析侧链）；"分析汇"->"分析抽头终点"。
- **baf_sas_analysis.md 更新**：§2 加 TID 分类修正；§3.3 加 FDP 双速率架构（含源码行号表）；
  §3.4 加 SlcMatrixMul 组件映射；§14.4/14.5/14.6 全面修正（5 抽头->4、FDP ✅、slc_matrix_mul ✅）。
- **Obsidian MDS6/03 更新**：新增 §2.5"双速率架构与主链内联"（速率转换表 + [!important] callout）；
  Part3 加 [!note] slc_matrix_mul 组件说明；§2.1 补 §2.5 引用。
- **UI ParamPanel.js**：参数面板头部布局修正--`node.id`（灰色参考）在上，`node.data.label`（粗体）配重命名按钮在中，
  `component`（灰色）在下。重命名按钮现在紧挨它实际修改的 label 而非 id。
- 测试：`test_distill_import.py` 5 项通过；拓扑展开验证 23 节点 / 13 子模块 / 4 downrate 抽头 / part2_fdp 在主链 / slc_matrix_mul 已映射。
## 2026-08-09（第四十一次讨论：估计/统计组件与调试可视化——第一批）

- 判断：PSD/相干/线性插值均为已知通用算法（相干公式与 SpeedBounds 插值已在
  `baf_sas_analysis.md` §11.6/§11.7 从源码蒸馏确认），无需再跑蒸馏，直接实现通用观测组件。
- 新组件（3 个，均音频直通 + readback 探针）：
  - `psd`：逐块 FFT + 指数平滑，`spectrum` readback（幅度数组，与 probe_spectrum 同构，
    复用频谱 widget）；
  - `coherence_matrix`：通道间交叉功率谱 EMA → 平均相干矩阵（N×N）+ 相干历史；
    readback `coherence`（{n, bins, matrix}）+ `history`（最近 64 块均值）；
  - `interp_lut`：一维线性插值查表（x_axis/y_axis BULK，x 输入 → y 输出），
    readback `y` + `history`（控制量历史曲线）。
- UI：`NODE_WIDGETS` 新增热力图（相干矩阵）与时间曲线（控制历史）两种 widget，
  psd 注册复用频谱 widget。
- 修复：psd/coherence_matrix 的 FFT 缓冲区按 half 分配、im 别名 re 导致堆越界
  （returncode 0xC0000374）——改为每通道 frames（2×half）独立分配。
- 测试 `test_estimation_components.py`（3 项）：正弦峰值 bin、全同信号相干≈1、
  查表插值数值；全量 110 passed、1 skipped。
- 意义：这是"估计/控制"那一半的观测侧落地（design_registry §20/§21）——
  跑 Audiopilot/ENC/RNC 蒸馏模型时，画布上可直接看功率谱/相干矩阵/控制量历史；
  真闭环（控制端口 + 参数投递）仍待后续。

## 2026-08-09（第四十次讨论：平台集成/外部接口节点——platform_hook 原型）

- 设计（design_registry §21）：把 embed_in/out 的「生成代码 + 用户填充」泛化为
  **声明式平台节点**类别——覆盖非音频 DSP 集成（amixer/通信/传感器），作为控制面（§20）落地前的过渡层。
- 实现：
  - manifest 新增 `execution.none: true`（声明式：不参与运行时执行，仅代码生成）；
  - 新组件 `orpheus.builtin.platform_hook`（无源码/无端口，参数 hook_name/interface/note）；
  - 编译器：execution.none 节点不进执行计划，收集进 `plan.declarations`（连线报错）；
  - resolve.py：声明节点不约束平台可达性（不把 PC 目标卡死）；
  - cli build：跳过 execution.none 组件（无运行时代码不构建）；
  - 生成器：`include/orpheus_platform_hooks.h` + `src/platform_hooks.c`
    （每个钩子 init/read/write + USER CODE BEGIN/END 段），CMakeLists 纳入编译。
- 测试 `test_platform_nodes.py`（3 项）：不进执行计划、不约束平台、生成钩子与 USER CODE；
  全量 107 passed、1 skipped。
- 边界：v1 只做无音频输入的纯声明节点；「有音频输入的观测汇」与控制面（CONTROL 端口 +
  参数投递，design_registry §20.3）后续再落地。

## 2026-08-09（第三十九次讨论：蒸馏多速率/task 建模——SKILL 与展开器）

- SKILL 更新（`SKILL/references/distill-model.md` + `SKILL/SKILL.md`）：
  - 分析步骤新增「任务/速率域（TID）识别」：提取全部 TID 的 rate_hz、call_interval（分频比），
    区分主音频链（interval=1）与分析侧链（interval>1）；
  - 新增 §4.1 多速率 task_flows 规范：主链用 `chains`（引用链 id）、分析侧链用 `chains`/`blocks`
    显式列出；`call_interval × rate_hz = 基块率`；纯缓冲/结构块不列入；
  - 映射表补充 iir_bank / rfft / ifft / input_mixer_3d / sleeping_beauty / gain_ramper；
  - 校验清单加 task_flows 完整性条目。
- `build_topology` 消费结构化 task_flows：interval=1 的链串接为主音频链（sys_in..sys_out）；
  interval>1 的 task 生成 `downrate(factor=call_interval)` 抽头 + 抽头子模块 + 分析抽头终点（embed_out），
  从 sys_in 抽头；旧格式（无 chains/blocks）回退为全部链串接。
- `examples/baf_sas_full.yaml` 重构：task_flows 结构化（TID0 主链 8 链，TID2 含 part2_fdp，
  TID1/3/4/5 为 Audiopilot 分析侧链）；audiopilot 链收敛为 TID0 正弦调制部分；
  噪声过滤补充 BufferRef/delayBuffer/RateTransition 等结构块。
- 结果：重新展开 22 主图节点（8 主链 + 4 downrate 抽头 + 4 分析抽头终点）、12 子模块；
  占位 13、真实映射 43（tap_5 的 magnitude²/saturation 从括号文本中拆出）。
- 测试：蒸馏导入断言更新（downrate factor 2/4/64/256/768 检查、part2_fdp 不在主链）；
  全量 104 passed、1 skipped。

## 2026-08-09（第三十八次讨论：baf_sas_analysis 缺口核查与补齐）

- 核查 `examples/baf_sas_analysis.md` 与 `baf_sas_full.yaml`：§13 声称已实现的 6 个组件
  （gain_ramper/iir_bank/rfft/ifft/input_mixer_3d/sleeping_beauty）在仓库中存在，但
  **从未接入蒸馏映射**（distill_topology / 前端徽章仍把 RFFT/pooliir/SleepingBeauty
  /InputMixer3D 当占位或错误映射），且 **DLL 未构建**、示例工程无自动化测试。
- 补齐：
  - 映射更新：pooliir→iir_bank、RFFT/FFT→rfft、IFFT→ifft、SleepingBeauty→sleeping_beauty、
    InputMixer3D/Downmix→input_mixer_3d（前后端同步）——重新展开占位 17 → 13、真实映射 38 → 40；
  - 构建 6 个组件 DLL；
  - 新增 `test_baf_components.py`（编译 + 离线端到端运行 + rms 探针），
    修正 `test_parse_flow` 旧断言（RFFT 现映射 rfft）。
- 剩余占位 13 个与 §14.2 一致：FDP 专用（Coeffs1st/2ndStage、ApplyCoefficients、PSD平滑、
  DetectImpulse、ReverbExtraction）、相干族（FormCoherenceMatrixGXY、30ch相干求和）、
  SpeedBounds（待 interp_lut）、输出总线结构（PreqOut1/AudioOut）、路由描述片段。
- 仍缺组件（§14.2）：`interp_lut`（查表插值，实现明确）、`coherence_matrix`（复数相干矩阵）；
  结构性缺口：多速率 TID 建模（§14.4，build_topology 串链坍缩速率域）与反馈环/任务桥。
## 2026-08-09（第三十七次讨论：目标平台与 alter 组件——后端核心）

- 设计定案（design_registry §19）：平台属性归组件自声明（缺省=无指定平台，可移植），
  alter 由用户在画布上声明（节点级 `alters`，同一层级），引擎只做合规校验与平台解析。
- 后端核心落地：
  - `platforms.yaml` 数据驱动平台表（v1 粗粒度 `win` / `dsp`，父平台隐含子平台，以后细分只改表）；
  - manifest 可选 `platforms`（registry 暴露）；project schema 加顶层 `target`（auto/win/dsp）
    与节点 `alters`；Node/Project loader 往返保留；
  - `resolve.py`：alter 组（同图无向并查集）→ 每节点可达平台（组内并集）→ 整链交集 →
    选平台（显式目标不在交集报错并列出断链节点；无交集报冲突节点）→ 逐组选激活成员
    （锚定优先，未激活成员移除、锚定连线重映射到激活成员，宏语义）；
  - alter 合规校验：端口集合一致、共享参数类型一致，否则编译报错；
    组内多个成员参与连线报错（同一槽位只能连一个）；
  - 编译器 `compile(project, target)` 先解析再走现有管线；
  - 平台标签：device_in/out → `[win]`、embed_in/out → `[dsp]`（其余无指定）。
- 测试 `test_platform_resolve.py`（8 项）：全平台可用性、按目标激活/重映射、断链冲突、
  目标不可达列出断点、接口不一致拒绝、多成员连线拒绝、编译路径解析、loader 往返。
- 全量 102 passed、1 skipped。UI（目标选择 + 「设为替代组件」+ 组徽标/置灰）待下一轮。

## 2026-08-09（第三十六次讨论：缺失组件——先实现实现明确的）

- 梳理 BAF SAS 蒸馏的 33 个占位块，先实现一批**实现明确**的通用 DSP 组件（8 个）：
  `switch`（开关/旁通，enable+斜坡）、`limiter`（峰值限幅，阈值/attack/release）、
  `soft_clipper`（tanh 软削波，drive 归一化）、`saturation`（饱和限幅，limit/soft 硬软切换）、
  `matrix_mul`（矩阵乘法，rows×cols BULK 系数）、`window`（窗函数，BULK 系数每块从头应用）、
  `noise_slew`（逐样本变化率限幅）、`level_detect`（峰值/RMS 包络检测 + level 探针 readback）。
- 全部注册进蒸馏映射表（后端 `distill_topology.py` 与前端 `ProjectTree.js` 徽章同步）：
  baf_sas_full 重新展开后占位块从 41 → 33，真实映射 23 → 31。
- 数值验证 `tests/test_dynamics_components.py`（6 项）：饱和削波边界、开关静音/窗加权、
  矩阵缩放、限幅稳态增益、软削波有界、变化率步长、电平探针——离线运行全部通过。
- 变长 BULK（matrix/window 系数）暂不注册运行期 BULK 槽（register_slots 先于 prepare，
  数量未知），与 fir 组件一致，仅 `kind: bulk` 走参数导入导出。
- 第二批：`square`（平方器 y=x²，功率/PSD 用）、`sine_mod`（正弦调制器，AM/颤音）。
- 蒸馏映射修正：按「块名+原始片段」匹配（Sum/SASOutputRouter/PostEQ 等此前映射不到），
  规则顺序调整（LevelDetect 优先于 pooliir、相干优先于窗/饱和、路由优先于 selector），
  并过滤数据表/描述性噪声块（*Map/路径/缓冲/置零/=powf 等）——baf_sas_full 重新展开
  占位 33 → 17，真实映射 38（第二批前为 31），剩余占位均为待调查项
  （FFT/STFT 族、FDP 专用、相干、SpeedBounds、SleepingBeauty、ReverbExtraction 等）。

## 2026-08-08（第三十二次讨论：工具栏布局修复）

- 状态提示从工具栏移出到独立 statusbar（固定高度 + 单行截断），保存/运行等长提示不再改变控件布局。
- 工具栏按逻辑分组（工程 / 结构 / 保存·调音 / 运行）加分隔，缓解控件拥挤。
- 进一步改为**两行工具栏**：主行=工程/保存/运行，副行=结构/调音/编译/代码生成（更轻量的控件尺寸），
  各行独立分组分隔；主行按钮/下拉缩小，整体不再挤。
- 两行观感不佳，改为**单行分组 + 展开**：工程组 ⋯（导入模型）、运行组 ⋯（编译/编译后运行/生成代码/
  下载 zip）收进下拉；常用项直接显示；点击工具栏外关闭。

## 2026-08-08（第三十三次讨论：节点管理——多选删除 / 组件归属 / 命名）

- **多选删除**：onNodesChange 删除时同步清理连边与选择集；结构组新增「删除选中」按钮（Delete 键同样可用）。
- **组件归属模型**：manifest 新增 `user_owned`（脚手架生成的自定义组件=true）。
  公共库（内置）不可删除；自定义组件可「删除」（确认 + 移除源码目录）或「提升为公共库」
  （提升后不可直接删除）；工程子组件删除加确认。后端 `DELETE/POST /api/components/{id}[/promote]`。
- **节点命名**：参数面板「重命名」——修改显示名（画布/参数面板定位用），不影响节点 id 与协议地址；
  原生与自定义组件实例均可。
- 测试 `test_component_management.py`（公共库拒删、提升后拒删、自定义删除）；全量 82 passed。

## 2026-08-08（第三十四次讨论：device_out 不是时钟源）

- 纠正：只有 out 没有 source 本就无法播放；把 device_out 标为 clock_source 是错误 hack，
  会掩盖“无源图通过校验”的假象。数据源才是时钟源（wav_in/signal_gen/device_in/embed_in），
  device_out 只是汇；播放回调驱动处理是 rt_host 的传输层细节。
- 移除 device_out 的 `clock_source`/`clock_domain`：时钟徽标消失；仅 device_out 的图被正确拒绝
  （无数据源可播）；有源的“仅播放”图（wav_in/signal_gen → device_out）仍正常（源提供时钟域）。

## 2026-08-08（第三十五次讨论：节点命名持久化）

- 重命名后的节点 label 此前只在内存（保存后重开丢失）。工程节点格式新增 `label` 字段
  （空=用 id 显示），Project Node/schema/loader/序列化与前端 graphToFlow/flowToGraph 全链路往返。
- 测试：PUT 带 label → GET 保留 → 编译不受影响；全量 85 passed。

## 2026-08-08（第三十六次讨论：参数面板选中同步）

- 现象：点击某些节点（尤其监控节点 ⤢ 内部元素）时视觉选中了、参数面板却没刷新。
- 根因：节点内交互元素 stopPropagation 吞掉 click，onNodeClick 不触发，selectedId 停留旧节点。
- 修法：onSelectionChange 同步——单选时设置 selectedId、空选时清空，覆盖所有“视觉选中但 click 没到”的情况。

## 2026-08-08（第三十七次讨论：左右侧栏可收起）

- 组件库与参数面板/子组件面板均可收起：画布左右边缘常驻细条按钮（«/»），收起后画布自动扩大。
- 收起的侧栏不渲染，编辑区最大化；再次点击边缘按钮恢复。

## 2026-08-08（第三十八次讨论：删除组件的引用防护 + 缺失节点标记）

- 现象：删除自定义组件后，画布上已拖入的节点仍保留，变成编译失败的无效节点。
- 防护：删除组件前扫描 workspace 工程（含子组件图）引用，被引用则拒绝删除并提示工程名。
- 画布：组件已删除的节点显示红色「组件缺失」徽标 + 红边框，便于定位清理。
- 测试：被引用拒删、移除引用后可删；全量 85 passed + 1 skipped。

## 2026-08-08（第三十一次讨论：自定义组件壳脚手架）

- `cli new-component <name>`：生成 ABI 骨架 + 用户文件隔离（`user/` 目录生成器永不覆盖）。
- manifest 新增 `custom_handles`（reply: true=response / false=notification），进 id_map（CUSTOM 类）；
  `resolve` 支持 CUSTOM 入口（无槽内存，按 ID 路由到组件 hook）；`/api/components` 暴露 custom_handles。
- 生成器复制组件 `user/` 目录（生成工程自包含）。
- 演示组件 `orpheus.builtin.my_effect`：user_handle 回显 CUSTOM 消息。
- 测试 `test_custom_component.py`（2 项）：脚手架文件隔离/内容；CUSTOM 消息走组件 hook 回显 +
  notification 无返回 + 离线运行直通。
- 全量 81 passed。

## 2026-08-08（第三十次讨论：二进制消息协议落地——CALL/RESPONSE/NOTIFICATION + call_id）

### 语义（并入 design_registry §18）

- 方向 runtime 向外：Response = 同步返回（所有 CALL 都有 RESPONSE）；Notification = 异步结果/事件推送。
- call_id：调用方自选不透明令牌（当 session/handle 皆可），callee 只回声；跨 CPU 异步返回按 call_id 回原调用者。
- 信封：8 字节头（route_id 一个 uint32 + msg_type 2b / flags 4b / call_id 16b / payload_words 10b），
  payload 4 字节对齐、消息自描述、小端。

### 实现

- ABI v3：`OrpheusMessageHeader`/宏、`OrpheusMsgType`、`OrpheusBlob`、`OrpheusEvent`、
  `OrpheusHookFn`；组件接口尾部追加统一 `hook`（旧 DLL 按 abi_version≥3 守门访问，避免越界读）。
- Runtime：`register_hook`（外部注册优先）、`message()`（CALL→RESPONSE 同步返回；NOTIFICATION 单向分发；
  分发优先级 外部 hook → 组件 hook → 默认槽语义；CUSTOM 必须由 hook 处理）、`build_notification`。
- 宿主：离线 `--msg <hex>`（支持 `--msg ... --run N --msg ...` 按命令行顺序执行）、`--echo-hook <id>`；
  rt_host `MSG <hex>`；后端 `POST /rt/msg`（RtSession 按 call_id 匹配 MSGRSP）。
- 生成侧：`orpheus_control_register_hook/message`（同款分发 + g_all_id_ref 路由 + 默认槽读写），
  生成 main 支持 `--msg`（多次，按序）。
- 测试 `test_message_protocol.py`（2 项）：RTC 写/读、PROBE 读/写拒、CUSTOM 无 hook 错误/echo hook、
  NOTIFICATION 无返回、BULK 双 bank 经消息通道写→run→读；动态与生成两路。

### 修坑

- ABI 尾部加字段必须升版本并按版本守门（旧组件 DLL 越界读 hook 崩溃）。
- `ORPHEUS_MSG_FLAG_ERROR` 必须是 4 位 flags 窗口内位（0x8），不能是绝对位 1<<29（会被 MAKE 再移位溢出）。
- 消息序列与块提交顺序：读回必须在 run（提交）之后；离线 CLI 改为按命令行顺序执行 msg/run。
- payload 按字对齐，尾部补零为设计语义（接收端按需去尾零）。

### 验证

- 全量 79 passed。

## 2026-08-08（第二十九次讨论：双 bank 落到生成路径 + 清理 backlog）

- 用户定调：双 bank 只有到最终生成的嵌入式代码才有意义；且实际场景是少数——通常先 mute 再更新。
- 实现：生成路径可选双 bank——`src/orpheus_control.c`（影子数组、槽表、按 node/key 与按 ID 的
  bulk 读写、`commit_bulk` 块边界提交）；生成 main 支持 `--write-bulk/--write-bulk-id/--read-bulk/
  --read-bulk-id/--run` 部署控制 CLI；`double_bank=off` 时零影子直写即时生效。
- 测试 `test_generated_double_bank.py`：auto 影子语义（写后旧值 → 跑 1 块后新值）、按 ID 读写、
  off 直写即时生效、off 无影子数组。
- 修坑：无 bulk 槽工程 `g_bulk_id_ref[] = {}` 触发 MSVC 内部编译错误 → 空表加哨兵条目；
  读回 CLI 必须在 process（提交）之后执行，不能在参数解析时内联读。
- 清理 §16 过时 backlog：BULK 双 bank、64 位 ID 两项标记完成（被 §17 取代）。
- 全量 77 passed。

## 2026-08-08（第二十八次讨论：双 bank 可选化——工程级 auto/on/off）

- 背景：双 bank 全局吃 2× 内存，部署时吃不消；可选后 UI 呈现要做到不啰嗦。
- 方案：声明在组件层（`ORPHEUS_SLOT_DOUBLE_BUFFERED` + manifest `bulk_slots[].double_bank`，
  只读徽标）；开关在部署层（工程 `double_bank: auto|on|off`，工程设置一个下拉，面板无逐行开关）。
- 实现：Project/schema/loader 增 double_bank；编译器折算生效位进 `plan.id_map[].double_bank`；
  Runtime 只对生效槽分配影子，未生效槽 `write_bulk` 直写 active（即时生效、零额外内存）；
  生成器 id_map 表/memory_map 标注；biquad_bank 声明双缓冲。
- 测试：auto（默认）→ 影子语义（写后未提交读旧值、跑 1 块后读新值）；
  off → 直写即时生效（写后立即读到新值）。
- 全量 75 passed；`npm run build` 通过。

## 2026-08-08（第二十七次讨论：BULK 双 bank 定层 + get_bulk 实现）

### 定层决策：双 bank 做在 Runtime

- 依据：仓库原则「临界资源保护/并发由框架提供，组件不自行实现线程同步」；双 bank 本质是并发原子性机制；
  提交时机天然是块边界（runtime 拥有 process_block）；内存成本与模块自管相同（2×），但组件零样板、
  语义统一、可观测。
- 落地：组件只注册一块 active 区；Runtime 为每个 BULK 槽分配影子区，
  `write_bulk` 越界检查后仅 memcpy 进影子并标记 pending，`process_block` 块边界一次性 memcpy 提交。

### get_bulk（确定实现，高速大块）

- `Runtime::get_bulk/get_bulk_id`：读 active bank，越界检查（count≤容量、offset+span≤state_size）
  后仅 memcpy；`lookup_id` 提供 node/key → ID 反查。
- 入口：rt_host `GETBULK <node> <key>` / `RGB <id>`（`BULKVALUE` 行）；
  离线 `--getbulk/--rgb`（支持 `--rwb ... --run N --rgb` 验证块边界提交）；
  后端 `POST /rt/read_bulk`；UI bulk 行「读回」按钮。
- 测试：写影子未提交 → 读回旧值；跑 1 块后 → 读到新值；按 (node,key) 读回 5 值。

### 验证

- 全量 75 passed；`npm run build` 通过。

## 2026-08-08（第二十六次讨论：三点收尾——后端 resolve API、按 ID 实时控制、动态模块连续分配）

### A. 后端与 UI 暴露 resolve（内存透明）

- 编译响应携带 `id_map`；`GET /rt/resolve?id=`（按 32 位 ID 查类型/长度/基址/偏移）、`GET /rt/map`（全表）。
- RtSession 支持按 ID 命令并捕获响应行（RESOLVED/RVALUE/OK RW 等）。
- UI 参数面板每行显示 0x ID（紫底），实时会话中点「解析」直接查内存地址。

### B. 按 ID 实时控制（RTC 通道）

- `Runtime::write_id/read_id/write_bulk_id`：方向只在接口，PROBE/STATE 拒写、命令拒读、模块包不直接读写。
- rt_host：`RW <id> <value>` / `RR <id>`（RVALUE 回显）/ `RWB <id> <n> <v0>...`；
  离线宿主同参：`--rw/--rr/--rwb`（可单进程先写后读验证）。
- 后端：`POST /rt/write`、`/rt/read`、`/rt/write_bulk`。

### C. 动态路径模块连续分配

- Runtime 按 `plan.modules` 递归布局（叶子按执行序 + 子模块紧随其后，8 字节对齐），
  每个模块（含根）一块连续内存；`resolve` 的 MODULE 条目现在有真实基址。
- 与生成路径嵌套结构体同一规则，动态/生成逐字节一致性测试仍通过。

### 验证

- `test_runtime_resolve.py` 扩展：模块包基址非空、`RW→RR` 单进程写读回 -6、PROBE 拒写、RWB 直写 OK。
- 全量 75 passed；`npm run build` 通过。

## 2026-08-08（第二十五次讨论：Runtime resolve 实现——内存透明落地）

- `plan.id_map` 成为唯一 ID 表（编译器按模块/槽/用途/形式生成），生成器改读 plan.id_map，
  动态 Runtime 加载同一张表 → 两路寻址一致。
- `orpheus_abi.h` 增 `OrpheusResolvedData`（id/kind/form/type/count/byte_size/module/slot/base/offset/node/key/name）
  与 `ORPHEUS_ID_SLOT_MODULE`（模块包槽号 0xFFFF，不与数据点槽冲突）。
- Runtime：`resolve(id)` / `resolve_all()`（数据点=真实地址；模块包=元数据，动态路径未连续分配 base=NULL）；
  plan 解析 id_map/modules。
- 宿主入口：离线 `orpheus_runtime --resolve <id>|--map`；rt_host stdin `RESOLVE <id>` / `MAP`。
- 测试 `tests/test_runtime_resolve.py`：RTC/TUNE/PROBE 三类 resolve（真实地址）、bulk 20B、
  模块包 0xFFFF、MAP 全表。
- 修坑：`operator<<(const char*)` 打印 NULL 崩溃（模块条目的 node/key 为空，需兜底空串）。
- 说明：manifest 参数未注册为运行槽（如 fir.coefficients 走 prepare）resolve 返回 NOT_FOUND，符合预期。

## 2026-08-08（第二十四次讨论：用途/形式正交分类 + RTC 排序第一）

- 纠正分类混维：BULK/MODULE 是**形式**不是用途——「TUNE 又是 BULK 形式」不再矛盾
  （一个子模块所有滤波器参数 = 用途 TUNE + 形式 FORM_MODULE 的一块连续内存；系数/波形是 FORM_BULK）。
- `OrpheusIdKind` 重排（按使用频率，RTC 第一）：`0x0 RTC` / `0x1 TUNE` / `0x2 PROBE` /
  `0x3 STATE` / `0x4 CUSTOM` / `0x5..0xF Reserved`；BULK、MODULE 从用途中移除。
- 新增 `OrpheusDataForm`（SCALAR/BULK/MODULE）作为独立维度，不进 ID 位，
  由 ID map 的 form/count/byte_size（CHAR_COUNT）描述。
- 生成器：bulk 参数/运行期槽 → 用途 TUNE + 形式 BULK；模块包宏 `ORPHEUS_MODULE_*` 用途=TUNE、
  形式=FORM_MODULE；`OrpheusIdEntry` 增 `form` 字段；memory_map.md 标注形式。
- 测试更新：模块包 ID = 0x10040000、`ORPHEUS_TUNE_FrontEqBankBq0Coefs`（不再有 ORPHEUS_BULK_* 用途宏）、
  id_map 含 FORM_MODULE/FORM_BULK。

## 2026-08-08（第二十三次讨论：RTC 语义修正——实时控制类，不只是命令）

- 澄清：RTC（real-time control）承载音量/fade/balance 等**实时可调参数**、一次性命令与实时信号输入——
  用户界面调，MCU 用该 ID 写 DSP。
- `OrpheusIdKind`：`0x1 CMD` 更名为 `0x1 RTC`（`ORPHEUS_ID_CMD` 保留为别名，命令在槽层以 COMMAND 标记）。
- 生成器 `_point_kind` 分类规则：update_policy 为 immediate/block_boundary/smoothed/transactional → RTC；
  restart_required / 影响签名 / 系数等 → TUNE；探针 → PROBE；bulk → BULK；state → STATE。
- 效果：gain_db/mute/mix 等实时参数生成 `ORPHEUS_RTC_*` 宏，biquad fc/q、channels 等仍是 `ORPHEUS_TUNE_*`，
  与「界面上调实时参数、MCU 用 RTC ID 操作 DSP」的语义对齐。

## 2026-08-08（第二十二次讨论：32 位数据 ID + 模块连续内存 + 生成 ID map）

### 定案（并入 docs/design_registry.md §17）

- 单 ID（uint32_t 宏）：不拆读写，方向只在接口；注册表按 kind 强制方向（PROBE/STATE 拒写、CMD 拒读），
  防「拿读 ID 写」靠接口+校验而非拆位。
- kind：TUNE/CMD/PROBE/BULK/STATE/MODULE/CUSTOM，其余 Reserved；CUSTOM 显式开放给用户自定义资源。
- 内存透明：`resolve(id)` 返回类型/长度/偏移；生成代码时产出 ID map 与内存布局，对照即知全部布局。
- flatten（执行拓扑）与连续内存（布局）正交：模块按子组件实例递归分配连续内存。

### 实现

- `orpheus_abi.h`：`OrpheusIdKind`（0x0..0xF，含 CUSTOM/Reserved）+ `ORPHEUS_ID_MAKE/KIND/MODULE/SLOT`。
- 编译器：plan 新增 `modules`（模块树 DFS 稳定 id、模块内叶子槽按执行序、嵌套归属正确）。
- 生成器：
  - arena 改为**模块嵌套结构体**（`include/orpheus_arena.h`：`OrpheusMod_*` + `OrpheusArena`），
    每个子组件实例一块连续内存，叶子 `state_block = &g_arena.<模块链>.<叶子>`；
  - 产出 `include/orpheus_ids.h`（`ORPHEUS_<KIND>_<模块><参数>` 宏 + `ORPHEUS_CHAR_COUNT_*`；
    单叶子模块=公司风格 模块+参数，多叶子模块带叶子名防冲突）；
  - 产出 `include/orpheus_id_map.h` + `src/orpheus_id_map.c`（静态 ID map，offsetof/sizeof 精确偏移，
    含 MODULE 整块条目与 CHAR_COUNT）；
  - 产出 `memory_map.md` 可读布局；CMake 自动纳入 id_map.c。
- 测试 `tests/test_ids_and_memory_map.py`（4 项）：模块布局稳定/连续/确定性、宏唯一与命名、
  ID map/memory_map 内容、嵌套子模块生成工程编译运行（dsp_model_reference）。

### 验证

- `cli build` 通过；`pytest` 74 passed；嵌套参考模型 run_generated 编译运行输出 WAV。
- 修坑：模块类型须后序定义（子类型先于父类型）；叶子成员名用末段而非完整节点 id；
  `(&g_arena.x)->src` 需括号（`&g_arena.x->src` 优先级错误）。

## 2026-08-07（第二十一次讨论：嵌入 I/O 占位组件 embed_in/embed_out + 生成代码适配模板）

### 目标

生成代码嵌入真实 DSP/MCU 时，source/sink 由用户按实际硬件填充。落地「易于填充」的占位组件：
组件侧只做内存拷贝（零 IO/零阻塞），生成工程输出带 USER CODE 段的 `platform_io.c` 适配模板，用户只需填三个函数。

### A. 组件

- `orpheus.builtin.embed_in`（时钟源，`clock_domain: embed`）：`process` 从 `state->src/src_frames` 拷贝输入，不足补零并累计 `underruns`（PROBE 探针）；参数 `channels`/`sample_rate`（restart_required + affects_signature，采样率接管图）。
- `orpheus.builtin.embed_out`（汇）：`process` 把输入拷贝到 `state->dst/dst_capacity`，不阻塞。
- 两个组件均 v2：公开状态结构体（`state_type`）、`register_slots`、`ORPHEUS_ENTRY_NAME`、CMake 组件库。
- 示例 `examples/embed_chain.yaml`：embed_in → gain → biquad → embed_out。

### B. 生成代码

- 存在 embed 节点时，`main.c` 生成非静态输入/输出缓冲 `g_embed_in_<node>`/`g_embed_out_<node>`、状态访问器 `orpheus_embed_in_state_<node>()` 等，init 绑定 src/dst，process 前后调用平台钩子。
- 生成 `src/platform_io.c`（含 `/* USER CODE BEGIN/END */` 段）：
  - `orpheus_platform_io_init()`：一次性初始化（DMA/编解码器）；
  - `orpheus_platform_io_pre_block()`：每块前填输入并设置 `src_frames`；
  - `orpheus_platform_io_post_block()`：每块后把输出交给 DAC。
- CMake 自动把 platform_io.c 加入构建；默认（不填充）静音运行，PC 一致性不破坏。
- 新增 `POST /api/projects/{name}/generate`（只生成不构建运行）与 UI「⤓ 生成代码」按钮，嵌入部署直接导出。

### C. 测试（`tests/test_embed_components.py`，5 项）

- embed 图编译（embed 时钟域）、动态离线静音运行（wav 全零 + underruns 上报）；
- 生成工程含 platform_io.c 模板、默认构建运行退出码 0；
- 「易于填充」数值验证：只改 pre_block USER CODE 段填 ramp，生成工程输出 ramp×gain 的 16-bit WAV（逐样本断言）。
- embed 组件进入参数面板分类（channels/sample_rate=setting，underruns=probe）。

### 验证

- `cli build` 新增 embed_in/embed_out DLL；`pytest` 全量通过；`npm run build` 通过。
- 调试插曲：wav_out 落盘为 16-bit PCM，测试一度按 float32 解包读成垃圾值——组件链路本身正确，修正测试解包。

## 2026-08-07（第二十次讨论：复杂嵌套参考模型 + 数据 layout 测试 + 模型蒸馏 SKILL + 一键导入）

### 目标

用户要为「蒸馏公司模型 C 代码 → 还原滤波器树 → 一键导入 Orpheus」的工作流打基础：
1. 造一个复杂嵌套模型并测试数据 layout（导出必须可读）；
2. SKILL 增加模型蒸馏说明；
3. 蒸馏文件（树形、标注滤波器类型与参数）可一键导入。

### A. 复杂嵌套参考模型 `examples/dsp_model_reference.yaml`

- 三层嵌套：主图 → `front`（前置处理）→ `eq_bank`；主图 → `crossover`（二分频）→ `low_band`/`high_band`；`post`（后处理）。
- 覆盖组件面：wav_in/out、gain、mute、biquad_bank（运行期 BULK 槽）、biquad（lowpass/highpass/highshelf）、fir（`kind: bulk` 系数）、delay、deinterleave/interleave、probe_rms、probe_waveform。
- 顶层 `model_tree` 可读树注释：标注滤波器类型与每个参数（蒸馏输出的示范格式）。
- 展开为 17 个原子节点、17 buffers，`cli compile` 通过；离线端到端运行输出 WAV，嵌套层探针以 flatId 上报（`front__mon.rms`、`post__fir.taps`、`out_mon.rms`）。

### B. 数据 layout 工具与测试

- 新增 `orpheus_core/parameter_catalog.py`：与前端 ParamBrowser 同构的编目逻辑（递归展开子组件、flatId=路径 `__` 连接、kind 分类），支持 `export_payload`（可读 JSON）/`apply_payload`（按 flatId 回写）/`render_tree`（文本树）。
- 新增 `scripts/parameter_layout.py`：打印数据 layout 树 + 导出 JSON 预览 + 回写往返校验。
- 新增 `tests/test_parameter_layout.py`（4 项）：flatten 层级、编译、编目分类（bulk/运行期槽/探针）、导出可读性与往返。
- **修复 `Project` 顶层未知字段往返丢失**：`presets`、`model_tree` 等 schema 放行字段此前 loader 重载即丢，现收进 `Project.extra`，保存→重载→导出不丢。

### C. SKILL 模型蒸馏

- `SKILL/SKILL.md` 新增「模型蒸馏」节 + 任务索引行。
- 新增 `SKILL/references/distill-model.md`：分析步骤（拓扑/原语映射/参数提取/分组）、滤波器→组件映射表、工程 YAML 骨架、`model_tree` 格式、校验清单、一键导入方式、红线与坑。

### D. 一键导入

- 后端 `POST /api/projects/{name}/distill`（body `{"yaml": "..."}`）：YAML 解析 + 形状校验 + 创建工程；顶层未知字段（model_tree/presets）保留。
- UI 工具栏「⤵ 导入模型」：选择 YAML → 提示工程名 → 创建并打开；导入示例同样可用。
- 测试 `tests/test_distill_import.py`（3 项）：导入往返（presets/model_tree 保留）、非法 YAML 拒绝、端到端运行（嵌套探针上报）。

### 验证

- `pytest` 64 passed（新增 7 项）；`npm run build` 通过；`python scripts/parameter_layout.py examples/dsp_model_reference.yaml` 布局树/导出/回写全部通过。

## 2026-08-07（第十九次讨论：参数预设/快照——保存-应用-删除）

### 目标

调音工作流闭环：把"改好了的一套参数"存成命名预设，随时一键应用/对比，随工程持久化。

### 实现

- 预设存储于工程文档顶层 `presets: [{name, created_at, nodes:[{node, path, component, values, bulk}]}]`（`viewsToDoc` 以 baseDoc 展开，自动随保存持久化；导入示例/打开工程原样保留）。
- 快照内容 = 调音值 + 工程 Bulk（FIR 系数等），不含探针实时值与运行期槽（只写、不持久化）。
- 参数面板顶部「预设」栏：保存当前为预设（prompt 命名）、预设列表（应用/删除）、时间戳。
- 应用预设复用导入回写通道（按 flatId 匹配视图节点 → 更新参数/端口 → 实时会话推送非重启参数），图结构变化导致节点缺失时提示并跳过。

### 验证

- `npm run build` 通过；`pytest` 57 passed（后端无改动）。

## 2026-08-07（第十八次讨论：运行期 Bulk 槽接入参数面板）

### 目标

上一步参数面板的 Bulk 只覆盖 manifest 参数（FIR 系数，随工程保存）；组件 `register_slots` 注册的运行期 BULK 槽（biquad_bank `bq*.coefs` 系数）此前只能由代码直写、UI 不可见。本次把运行期 Bulk 槽通过控制协议暴露进参数面板：声明可见 + 实时会话直写 + 文件导入。

### A. 协议与后端

- `rt_host.cpp` stdin 协议新增 `BULK <node> <key> <n> <v0> <v1> ...`：解析 n 个 float 调 `runtime.write_bulk`（已有边界校验），回显 `OK/ERR BULK`。
- `RtSession.write_bulk`（`server/rt.py`）构造协议行；`POST /api/projects/{name}/rt/bulk`（`server/app.py`）接收 `{node, key, values: [float]}`。
- manifest schema 新增 `bulk_slots` 声明（id/name/type/count/unit）；`biquad_bank` 声明 `bq0.coefs`/`bq1.coefs`（各 5 个 float）。`/api/components` 暴露 `bulk_slots`。
- 新增 `tests/test_rt_protocol.py`：BULK 行格式单测（2 项）。

### B. 前端

- 参数面板 Bulk 分类下区分两类：工程参数（FIR 系数，编辑/整体导入导出）与**运行期槽**（显示 `N/5` 计数 + 「从文件导入」（JSON 数组或文本数值，数量校验）+「写入实时会话」按钮，非运行态禁用）。
- 运行期槽不随工程导出/导入（会话内可写、不持久化，界面有标注）。
- `App.js` 新增 `onWriteBulk`：调 `rtWriteBulk` 并反馈状态。

### 验证

- `cli build` 全量重建通过；`npm run build` 通过；`pytest` 57 passed（含 2 项新协议单测）。
- 运行期 BULK 直写的效果即 design_registry 已验证的闭环：默认 rms 0.3409 → 直写系数后 0.1704，越界写入被 runtime 拒绝。

### 已知/后续

- 运行期 Bulk 只写不读：读回需 `get_bulk`（设计文档 ID/协议项，未做）。
- 参数快照/预设（整工程保存-对比-恢复）仍是下一个候选方向。

## 2026-08-07（第十七次讨论：全局参数面板 + 导入导出（含 Bulk））

### 目标

组件/数据点增多后，用户不应逐层点进节点改参数，又要能知道每个数据点在哪个子系统。本次落地"按数据类型分类的树形全局参数面板"，支持整体/单个导出导入，Bulk 数据（如 FIR 系数）单独建模与文件导入。

### A. manifest 数据点类别 `kind`

- `component_manifest.schema.json` 参数新增可选 `kind: setting|command|bulk|probe|state`（缺省按 readback+persistent 推断：探针，否则 setting），与 design_registry 的槽模型对齐。
- `fir.coefficients` 标记 `kind: bulk`（首个 Bulk 数据点；C 侧/编译/生成不受影响，纯 UI 语义）。

### B. 参数面板（`ui/src/ParamBrowser.js`，工具栏「☰ 参数面板」）

- 树形组织：数据类型（调音参数 / 大块数据 (Bulk) / 探针 / 命令 / 状态）→ 子组件层级 → 节点 → 数据点。
- 遍历主图与子组件视图递归展开；flatId = 实例路径按 `__` 连接（与 `flatten_project` 同规则），运行时可对任意嵌套叶子直接 `SET`。
- 调音参数：复用 `widgets.js` 控件编辑；实时会话中非 `restart_required` 参数即时推送（flatId 直达，顺带修复子组件内部节点此前无法实时调参的盲区），需重启参数给提示。
- Bulk 行：值计数 + 文本编辑 + 单参数从文件导入（JSON 数组或逗号/空格文本）与导出。
- 探针：只读，优先显示实时值（`rt.probes[flatId]`）。
- 搜索过滤（参数名/节点/组件）、面包屑路径、每节点「定位」按钮（跳转对应标签页并选中节点）。
- 导出：全工程 JSON（`node`=flatId、`path` 面包屑、`values` 调音值、`bulk` 数值数组、`probes` 实时快照）；导入：按 flatId 匹配写回视图参数（Bulk 数组回写为逗号串），并即时推送非重启参数。

### C. 顺带修复：示例工程路径与导入

- 回归测试暴露 8 个示例的 `file_path` 指向旧仓库绝对路径 `C:\D\Code\git\Orpheus\...`（本机不存在），导入后 wav_in 被错误改写为 `outputs/test_input.wav` 导致 e2e 连锁失败；`build/` 目录陈旧（缺 sweep_record/resample 目标、runtime 未更新）导致另 5 个失败。
- 修复：示例改为工程相对路径（输入 `test_input.wav`、输出 `outputs/*.wav`）；`manager._import_example` 支持相对路径相对示例目录解析并拷贝（示例可移植），输出/不存在文件仍落 `outputs/`（保持历史行为）。
- `python -m orpheus_core.cli build` 全量重建后 `pytest` 55 项全过（此前 11 失败均为上述环境/示例问题，与面板改动无关）。

### 涉及文件

- 前端：`ui/src/ParamBrowser.js`（新）、`App.js`（工具栏入口 + `onNodeParamChange`/`onImportApply`/`onLocateParam`）、`App.css`
- 后端：`orpheus_core/schemas/component_manifest.schema.json`（kind）、`orpheus_core/server/manager.py`（示例相对路径导入）、`components/.../fir/component.yaml`（kind: bulk）、`examples/*.yaml`（8 个路径修正）

### 验证

- `npm run build` 通过；`python -m pytest orpheus_core/tests/` 55 passed。

### 已知/后续

- 运行时 BULK 槽（如 biquad_bank `bq*.coefs`）目前只在会话内可写、不可持久化；面板 Bulk 基于 manifest `kind: bulk` 参数（FIR 系数）。后续可按 design_registry 的 ID/协议项加 `BULK` 命令与槽枚举，把运行时槽也纳入面板与导入导出。
- 子组件参数提升（mask）仍为 v1 未实现；面板以 flatId 平铺绕过了"必须逐层点进"的痛点。

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

## 2026-08-06 设备通道解耦 + 能力校验 + 工程级全局配置(buffer_size) + 源驱动采样率

### 背景（用户两问 + 指令）
1. device_in 调整通道的影响？系统音频通道数不是固定的吗？
   指令：device_in/device_out 通道数各取各值、互不相关；设备不支持->报错；设备会做转换->告警。
2. block_size 等参数是全局还是按 source 配置？
   指令：增加工程级全局参数配置机制并开放接口与配置界面（如 buffer 大小）；block_size/sample_rate 由 source 决定，source 支持配置则放入 source 配置并按实际校验范围。

### A. 设备通道解耦 + 能力校验（rt_host）
- `HostContext.channels` 拆为 `in_channels`（采集侧=device_in 端口通道）/ `out_channels`（播放侧=device_out 端口通道），二者独立。原代码用单一 `host.channels`，device_out 通道在 device_in 存在时被忽略——已修复。
- 采集/播放设备分别按各自通道打开；duplex 单设备也支持 capture != playback 通道数。
- 异步桥环形缓冲按 `in_channels`（承载原始采集 PCM）；rb_capture 用 in_channels；rb_playback 用 in_channels 读环形缓冲入 device_in_buf、用 out_channels 写 device_out_buf 出播放。
- 能力校验 `check_device_caps()`：经 `ma_context_get_device_info` 取 `nativeDataFormats[]`（channels/sampleRate，0=任意）。存在同时匹配通道与采样率的条目->native（LOG）；否则->`LOG WARN ... will convert (channels X, rate YHz)`；取不到信息->unknown。真正不支持->`ma_device_init` 失败报错退出（ERROR）。注：miniaudio 几乎总以转换方式打开设备，故"不支持"=init 失败，"会做转换"=非原生(warn)。
- 验证：默认播放设备(VB-Cable) 2ch/48k -> native；请求 8ch/88200Hz -> `WARN will convert`，`in_channels=2 out_channels=8`，运行正常。

### B. 工程级全局配置 buffer_size + API + UI
- `Project.buffer_size: int = 0`（0=自动=sample_rate/10，保持原硬编码行为）；project_to_dict/ProjectLoader/project.schema.json/manager.create 同步。
- `ExecutionPlan.buffer_size`（compiler 从 project 写入）；C++ `Plan.buffer_size` + plan.cpp 解析；rt_host 读 plan.buffer_size（0 则 sample_rate/10）作为异步环形缓冲容量。plan.json 已携带该字段，app.py 无需改 CLI 调用。
- UI：新增 `ui/src/ProjectSettings.js`（模态），工具栏"⚙ 设置"按钮；编辑 sample_rate/block_size/buffer_size，保存写回 doc 并置 dirty（viewsToDoc 以 baseDoc 展开保留这些字段）。App.css 加 .settings-field/.settings-hint。npm run build 通过。

### C. 源驱动采样率(sample_rate) + 实际校验
- device_in/device_out 新增 yaml 参数 `sample_rate`（int, default 0=继承工程全局, range [0,192000], restart_required, affects_signature；与 device/source 一样为 yaml-only，C 描述符不变）。
- compiler `_resolve_source_rate()`：扫描 device 源节点的 sample_rate 参数，若声明则采用为图形编译期采样率（覆盖工程默认）；多源不一致->CompileError；未声明->沿用 task.sample_rate。block_size 保持工程全局（图形调度量子，非设备属性；设备周期已与之解耦）。
- rt_host 以 plan.sample_rate（已源驱动）打开设备并按 nativeDataFormats 校验（见 A）。端到端：device_out sample_rate=88200 -> plan.sample_rate=88200 -> rt_host 按设备实际校验告警。
- 设计取舍：单任务调度器决定整图单一采样率，故 sample_rate 源驱动="源声明、编译期采用、运行时按设备校验"，而非每源独立运行不同采样率（多采样率需 resample 组件 + divisor，已有机制）。

### 涉及文件
- C++：`orpheus_runtime/src/rt_host.cpp`、`include/orpheus_runtime/plan.h`、`src/plan.cpp`
- Python：`orpheus_core/project.py`、`schemas/project.schema.json`、`server/manager.py`、`compiler.py`、`server/app.py`（plan.json 已带 buffer_size，rt_host 读，无需改调用）
- 组件 yaml：`components/orpheus/builtin/device_in/component.yaml`、`device_out/component.yaml`
- UI：`ui/src/ProjectSettings.js`(新)、`App.js`、`App.css`

### 验证
- `cmake --build build --target orpheus_rt_host` 通过。
- pytest：clock_rate/variable_ports/subgraph 等通过；3 个 test_server 离线探针测试在 clean tree 也失败（pre-existing，非本迭代引入，疑似离线宿主静音问题）。
- compiler 源驱动采样率 4 case（默认/单源/双源一致/双源冲突）正确。
- rt_host 实时冒烟：native 与 convert(WARN) 两路径日志正确，通道解耦生效。

### 已知/遗留
- 离线宿主 3 个探针测试 pre-existing 失败（signal_gen->rms 读数 0.0），与本迭代无关，未修。
- block_size 仍为工程全局（设计决定，见 C 取舍）。

## 2026-08-06 实时缓冲水位计 + 缓冲预充(根治慢性欠载) + block_size 时间提示

### 背景
用户反馈：调大 buffer_size 至 500ms 仍有 `LOG WARN 播放欠载 x20/s` 杂音。疑问：是否 source/sink 不同步？能否增加水位实时显示与上下溢提示（不破坏架构）。

### A. 实时水位计（PROBE_JSON __host__，已完成并补文档）
- rt_host `probe_thread`（每 200ms）在 `report_probes` 后额外输出 `PROBE_JSON __host__ rb {level,capacity,primed,underruns,overruns,bridge}`：level=ma_pcm_rb_available_read，capacity=rb_capacity。
- 后端 `rt.py` `parse_probe_line` 已通用解析 PROBE_JSON 进 probes["__host__"]["rb"]；UI 每 250ms 轮询 rt/status，实时日志面板渲染水位条（绿/黄/红按填充率）+ 文本 `level/capacity 帧 (pct%) · 欠载 N · 溢出 N`；非桥模式显示「设备时钟模式」。不新增后端接口，不改节点探针映射（__host__ 不匹配任何节点，被忽略）。

### B. 缓冲预充（根治「调大缓冲仍欠载」的根因）
- 根因：异步桥环形缓冲从空启动，播放回调立即拉取，缓冲始终在 0 附近游走；即使采集/播放速率匹配，任何瞬间采集未跟上即欠载 -> 慢性 20/s 欠载。增大 buffer_size 无济于事（缓冲根本没机会填起来）。
- 修复：HostContext 增 `primed`(atomic bool) + `prime_target`(=容量/3)。`rb_playback_callback` 在未 primed 时，若水位 < prime_target 则输出静音并 return（不消费、不计欠载）；水位达标则置 primed=true 开始正常消费。
- 效果：采集先填满 1/3 缓冲再让播放消费，建立吸收时钟漂移与调度抖动的垫层；正常速率匹配时不再慢性欠载。
- 诊断分支：2s 未达水位 -> `LOG WARN 缓冲预充不足`（采集未供数：loopback 目标未播放/采集设备异常），播放持续静音，水位计 level=0；预充后 level 持续走低 -> 真实时钟漂移（改用同一设备 in/out）。预充完成 -> `LOG 缓冲预充完成，开始播放`。
- UI 水位计：未 primed 时显示「预充中… level/capacity 帧 (pct%) · 等待采集供数」（蓝色），与正常/告警状态区分。

### C. block_size 时间提示（用户「可以，增加提示」）
- ProjectSettings 块长度字段旁加只读提示 `当前块 ≈ X.XXX ms（按工程采样率换算：ms = 块帧数/采样率×1000）`，3 位小数避免 2 的幂次帧数看起来是整数。采样率被设备源覆盖时以运行时为准。

### 涉及文件
- C++：`orpheus_runtime/src/rt_host.cpp`（HostContext primed/prime_target；rb_playback_callback 预充；probe_thread PROBE_JSON primed 字段 + 预充告警/完成日志）
- UI：`ui/src/App.js`（水位计预充分支）、`ui/src/ProjectSettings.js`（block_size ms 提示）
- 文档：`SKILL/references/run-debug.md`（PROBE_JSON __host__ 约定、水位警告修正 block_size->buffer_size、预充说明、故障排查两行）

### 验证
- `cmake --build build --target orpheus_rt_host` 通过。
- `npm run build` 通过。
- 3 个 test_server 离线探针测试仍 pre-existing 失败（与本迭代无关）。

### 已知/遗留
- 预充阈值固定为容量 1/3（未开放配置）；如需更小延迟可调小 buffer_size。
- 预充仅在异步桥模式生效（duplex 单设备同时钟无需预充）。


## 2026-08-10 BAF SAS step0 结构继续展开

### 完成项
1. `post_process` 子组件已在 `examples/baf_sas_step0.yaml` 中替换为真实 PostProcess 链路：pre_ramp → limiter → post_eq → soft_clipper → mute_ramp → calibration → freq_comp → output_delay。
2. Medusa Part3 矩阵混音展开为 3 个 `slc_matrix_mul`（Cs 13→2 / Left 13→10 / Right 13→10）+ output_router 合入 22ch Merge 总线，保留 IIR 斜坡能力（ramp_coeff=0.995842）。
3. Part4 / PostProcess 中的 IIR 实例统一改为 13 级 per-channel 占位（`num_stages: 13`，`coefs_mode: per_channel`，系数全部为单位 IIR），与 Model_1_1.c 中 pooliir 13 级结构对齐。
4. Part4 22ch 重排序已按 Model_1_1.c:13215 的 tmp[] 表实现（1-based 索引）。

### 验证
- `python -m orpheus_core.cli compile examples/baf_sas_step0.yaml` 通过。
- `python -m pytest orpheus_core/tests/test_baf_step0.py -q`：4 passed。
- `python -m pytest orpheus_core/tests/ -q`：117 passed, 1 skipped。

### 遗留 / 待下一步
- 真实 IIR 系数（PostEQ/FreqComp/FullRateEq/MixEq）需要从 `Model_1_1.c` 提取；当前文件不在仓库内，需用户提供路径。
- Part3 的 `slc_matrix_mul` 当前使用 identity 占位表；真实 `CsTargetGains[26]` / `LeftTargetGains[70]` / `RightTargetGains[70]` 同样需从 Model_1_1.c / PingPongStruct.xml 获取。
- `limiter` 组件仍为单共享包络，BAF 需要每通道 attack/decay/k1/maxAttack，待扩展组件。
- Part6→MusicIn 反馈当前为 identity 22→30 扩展，需按 BAF 反馈索引表精确映射。
- Audiopilot 子组件仍为占位，需进一步展开 HF/LF 噪声滤波与增益计算。

## 2026-08-10 从 Model_1_1.c 校正 IIR 级数

- 已定位 `C:\D\Work\Project\EREV\cart-cicd-erev\components\baf\src\out\baremetalgxp\slx\code\Model_1_1_ert_shrlib_rtw\Model_1_1.c`。
- 从对应 TOP 文件确认默认 IIR 级数：
  - PostProcess `PostEQ` / `FreqComp`：`NumStages = [1]*22`
  - Medusa Part4 `FullRateEq`：复用 p8 `MedusaFullRateHoligramIirPooliirNumStages = [8]*22`
  - Medusa Part3 `MixEq`：`MedusaFullRateMixEqPooliirNumStages = [10]*13`
- 已按上述级数更新 `examples/baf_sas_step0.yaml` 中 `post_eq`、`freq_comp`、`full_rate_eq`、`mix_eq`，系数仍用单位 IIR 占位（默认调音数据未提供具体双二阶系数）。
- 验证：`python -m pytest orpheus_core/tests/ -q` → **117 passed, 1 skipped**。

> 注：TOP 文件中 pooliir 系数数组为单位矩阵形式，与 `iir_bank` 的 `[b0,b1,b2,a1,a2]` 双二阶格式需进一步映射；当前示例保持可运行占位，待真实调音系数确定后再精确填充。
