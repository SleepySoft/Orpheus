# Orpheus 基础版本实施日志

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

