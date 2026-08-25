# 控制链路与参数可视化连接 — 可行性评估

> 评估问题：在现有音频链路可视化基础上，加入"控制链路"（组件输出控制值 → 驱动另一组件的参数）的可视化连接；界面可开关控制连接的显示；控制连线与音频连线区别显示，线上标注参数形状，并做参数匹配检查（含数组/矩阵等随配置变化的签名）。

## 结论（TL;DR）

**可行，且基础设施已预留一半。** 但必须明确：这不是纯 UI 特性，而是贯穿 manifest schema → registry/compiler → runtime/generator → UI 的横向特性。建议分三期落地，**第 0 期（UI 可视化 + 静态匹配检查）可独立交付**，运行时语义（第 1/2 期）可后续叠加。

---

## 一、现状盘点（决定可行性的关键事实）

### 1.1 已预留的伏笔

| 位置 | 事实 |
|---|---|
| `orpheus_abi/include/orpheus_abi.h:70-73` | 端口类型枚举 `ORPHEUS_PORT_AUDIO=0 / CONTROL=1 / BULK=2 / DEBUG=3`，CONTROL 已预留未使用 |
| `schemas/component_manifest.schema.json:62` | 端口 `type` 枚举已含 `control|bulk|debug`，manifest 声明 control 端口**今天就能通过 schema 校验** |
| `compiler.py` | 对 control 端口不做特殊处理（仅类型相等校验），不会报错 |
| `ORPHEUS_ID_RTC`（orpheus_abi.h:131） | 32 位 ID 预留"实时信号输入"语义，未实现为图边 |

### 1.2 尚不存在的部分

- **图内参数驱动参数的机制完全不存在**：没有 sidechain / modulation / control_out 概念；`level_detect` 的电平只经 PROBE 上报到 UI 为止，无法回灌图内其它节点。
- **运行时无控制值投递路径**：组件间参数传递只有编译期特判硬编码（compiler.py:265-291，wav_out 跟随源采样率、sweep_record 跟随 sweep_gen）。
- **UI 无任何控制连线痕迹**：onConnect（App.js:506-520）唯一校验是"输入引脚独占"，无端口类型检查、无 isValidConnection、无自定义 edge、无 edge label。

### 1.3 参数"形状"的现状（本需求的最大缺口）

参数类型**只有 float/int/bool/string 四种**，数组/矩阵没有正式维度声明：

- 数组/矩阵 = `type: string, kind: bulk` 的逗号分隔文本（fir.coefficients、matrix_mul.matrix、iir_bank.coefs），维度由**独立的 int 参数**承载（matrix_mul 的 rows/cols，均 `affects_signature: true`）。
- `param:xxx` 引用目前只出现在**端口签名/调度表达式**（`channels: param:cols`、`count: param:channels`、`divisor: param:factor`），由 compiler `_resolve_value`（compiler.py:51-89）求值。
- plan.json 中参数是未求值的扁平键值串，矩阵就是 `"1, 0, 0, 1"` 字符串。

**结论：要做"线上标注参数形状 + 形状匹配检查"，必须先给参数（和控制端口）补 `shape` 元数据**，好在这可以复用端口表达式的同一套求值机制。

---

## 二、总体方案分层

```
第 0 期：可视化 + 静态检查（无运行时语义变化）
  ├─ manifest：参数/端口增加 shape 声明；参数标记可绑定（control target）
  ├─ UI：控制连线显示开关、区别化连线、形状标注、连接时匹配检查
  └─ compiler：控制连接的形状求值与校验（编译即报错）
第 1 期：块边界参数推送（runtime 语义，推荐首选）
  └─ 控制源（probe 类槽）每 N 块由 runtime 路由到目标 set_parameter
第 2 期：控制速率信号端口（sidechain，完整控制链路）
  └─ control 端口携带控制速率 float 缓冲，组件 process 内读取
```

第 0 期交付后，图上的控制连接是"带校验的声明"；第 1 期让它真正动起来。第 2 期是终态（音频rate/控制rate的连续调制），但工作量最大，且必须同时改动态加载与代码生成两条路径（仓库红线：两路径逐字节一致，有一致性测试卡着）。

---

## 三、数据模型设计（第 0 期核心）

### 3.1 manifest 扩展

**参数侧**（component_manifest.schema.json parameters 增加可选字段）：

```yaml
parameters:
  - id: matrix
    type: string
    kind: bulk
    shape: [param:rows, param:cols]   # 新增：形状表达式列表，复用端口表达式语法
  - id: gain
    type: float
    bindable: true                  # 新增：允许作为控制连接目标（缺省 false）
```

- `shape` 元素为整数常量或 `param:xxx` 表达式；缺省 = 标量 `[]`。
- `bindable: true` 隐含约束：`update_policy` 必须是 `immediate|smoothed|block_boundary`（`restart_required` 参数禁止作为控制目标，编译期校验）。

**端口侧**：控制输出复用已预留的端口类型：

```yaml
ports:
  - id: level
    name: 电平输出
    type: control
    direction: output
    shape: [param:channels]          # 同样支持参数化形状
```

`level_detect`、`probe_rms`、`anc_fxlms`（noise_reduction_db）等现有 probe 参数可平滑升级为控制输出端口。

### 3.2 工程文档（project.yaml）扩展

现有 `connections` 的 `from/to` 是 `"node:port"` 字符串，端口与参数同命名空间会歧义。**建议新增独立段**，不动现有 connections 格式：

```yaml
control_connections:
  - from: "level1:level"      # node:control_port
    to: "gain1:gain"          # node:param（bindable 参数）
```

独立段的好处：旧版加载器直接忽略（向后兼容）；compiler 单独校验；UI 的 graphToFlow/flowToGraph 往返映射简单。

### 3.3 形状求值

复用 compiler `_resolve_value` 的原子解析（`param:<id>` + 整数运算链），对控制连接两端分别求 shape：

- 源端：control 端口的 `shape` 按源节点 params 求值；
- 目标端：参数的 `shape` 按目标节点 params 求值（如 matrix_mul.matrix → `[rows, cols]` = `[2,2]`）；
- 标量参数 shape = `[]`，与 `[]` 或 `[1]` 的源匹配（规则需明确，建议 `[1]` 可连标量，反之不行）。

---

## 四、UI 设计（第 0 期核心）

### 4.1 显示开关

- 直接抄现有工具栏 checkbox 模式（自动保存、paceRun，App.js:1451-1454）：工具栏加「控制链路」勾选，状态存 localStorage（先例：`orpheus.leftWidth`）。
- **关闭时**：传给 ReactFlow 前过滤掉控制 edges，**且 OrpheusNode 不渲染控制 handle**（节点 data 注入全局标志或经 React context 下发）——界面与现在逐像素一致，满足"连控制连接点也不显示"。
- **开启时**：控制 handle 出现在节点上（建议输出控制口在右侧、可绑定参数做成左侧或参数行内的目标 handle），样式区别于音频圆点（如菱形/方形 + 不同颜色，如橙色 `#f0a24c`）。

### 4.2 连线区别显示 + 形状标注

- 引入自定义 edge 类型（目前无 `edgeTypes`，App.js 仅定义 nodeTypes）：控制边用**虚线/动画虚线 + 控制色**，与音频 bezier 实线区分。
- 边上 label 显示**求值后的形状**：如 `标量`、`[2]`、`[2×2]`。求值逻辑与 `resolveExprValue`（graphUtils.js:19-27）同源，按两端节点当前 params 计算；源/目标形状不一致时 label 变红并加 ⚠。

### 4.3 连接时匹配检查

在 `onConnect` 增加校验（目前只有输入独占检查）：

1. **类型检查**：控制 handle 只能连控制目标（bindable 参数 handle），音频口只能连音频口；跨类型直接拒绝。
2. **形状检查**：两端 shape 表达式按当前 params 求值，不匹配则拒绝并在 status bar 报中文原因（如「形状不匹配：[3] → [2×2]」）。
3. **策略检查**：`restart_required` 参数无 bindable handle，天然不可连。

### 4.4 动态签名的联动（本需求点名的难点）

仓库已有成熟机制可镜像：参数变化时 `onParamChange`（App.js:567-601）重算 `resolvePorts` 并**剪掉 handle 已失效的边**。控制链路照搬：

- 影响源端 shape 的参数变化（如 matrix_mul 的 rows/cols）→ 重算相关控制边两端形状 → 形状不再匹配的边**标红保留**（比直接剪线友好，用户改回参数即恢复；音频边维持现有剪线行为即可）。
- `affects_signature` 参数本就需要重编译，控制边校验也随编译 API 在后端复核（见下）。

### 4.5 持久化往返

- `graphToFlow`/`docToViews`：读 `control_connections` 段生成控制 edges（edge data 标记 `kind: 'control'`）。
- `flowToGraph`/`viewsToDoc`：控制 edges 单独写回 `control_connections`，音频 edges 维持原格式。
- 注意 `id` 生成规则避免与音频边 `e-<from>-<to>` 冲突。

### 4.6 后端校验 API

编译是唯一"校验"入口（`POST /api/projects/{name}/compile`）。compiler 在现有 `connections` 校验旁加 `control_connections` 校验（存在性、bindable、update_policy、shape 匹配），错误进编译报告，UI 编译时即可暴露失配。

---

## 五、运行时语义（第 1/2 期概要，本期不实施）

### 第 1 期：块边界参数推送（推荐先行）

- compiler 把控制连接编译进 plan 新段 `control_links: [{src_node, src_slot, dst_node, dst_slot, shape, divisor}]`。
- runtime 每块（或每 N 块）读源槽值（复用 probe_slots/get_parameter 路径），写目标 `set_parameter`。**RT 红线**：写动作必须与音频线程的关系明确——建议在音频回调块边界、同线程内调用，禁止跨线程锁；目标参数仅限 `immediate/smoothed`。
- 生成路径同步：generator 模板输出等价的静态轮询代码，跑一致性测试。
- 串行链路（SerialSession/OLINK）天然兼容：控制值本质就是 RW 槽写。

### 第 2 期：控制速率信号端口

- control 端口携带浮点缓冲（控制 rate，如每块 1 个值或 audio rate 全速率），runtime buffer 分配复用音频 buffer 路径，签名中 channels 即 shape 展平长度。
- 组件 process 判空读取控制输入（多端口组件"未连接引脚为 NULL"的既有约定直接适用）。
- 这是 sidechain 压缩、包络跟随调制的终态，但工作面最大。

---

## 六、工作量与风险

### 工作量粗估

| 期 | 范围 | 量级 |
|---|---|---|
| 0 | schema(shape/bindable) + UI(开关/handle/自定义edge/校验/持久化) + compiler 校验 + 2~3 个示例组件升级 | 中（UI 占 60%） |
| 1 | plan 新段 + runtime 块边界推送 + generator 模板 + 双路径一致性测试 | 中 |
| 2 | control 缓冲端口 + buffer 分配 + 组件改造 + 一致性测试 | 大 |

### 风险清单

1. **双路径一致性**（仓库红线）：第 1/2 期必须在动态加载与代码生成两条路径实现等价语义，`test_generated_run_matches_dynamic_run` 会卡。第 0 期无运行时语义，不受影响。
2. **RT 安全**：第 1 期 set_parameter 进音频线程需谨慎，组件实现内禁止 malloc/锁；先在 `block_boundary` 策略参数上试点。
3. **shape 表达式能力**：复用 `param:` 求值即可覆盖 rows/cols/channels 类场景；暂不引入新表达式语法，避免 compiler/UI 两套求值器发散。
4. **旧工程兼容**：`control_connections` 独立段 + manifest 新字段全可选，旧工程、旧组件零影响。
5. **UI 复杂度**：OrpheusNode 已承担端口行/徽标/探针染色/nodeWidgets，控制 handle 再加一层需注意可维护性，建议控制 handle 渲染独立成子组件。

## 七、建议的落地顺序

1. schema 加 `shape`/`bindable`（向后兼容）+ compiler 校验 + matrix_mul/fir/level_detect 三个组件试点声明；
2. UI 第 0 期全量（开关、handle、虚线 edge、形状 label、onConnect 校验、动态签名联动、持久化）；
3. 示例工程（level_detect → gain 的标量链路、probe_rms → matrix_mul 的矩阵链路）+ pytest（编译校验、往返序列化）；
4. 评估第 1 期运行时推送的真实需求强度后再动工。

---

## 八、关键设计决策（Q&A 沉淀）

### 8.1 代码生成形态：注入式，经标准 ABI 槽

**结论：注入式**，不用"全局变量+引用式"。生成代码在块循环中显式完成"读源槽 → 写目标参数"：

```c
/* ==== 控制链路（块边界执行，延迟固定 1 块） ==== */
static void control_tick(void) {
    /* level1.level [标量] -> gain1.gain */
    float ctl0;
    if (orpheus_get_parameter(&inst_level1, LEVEL1_LEVEL, &ctl0) == ORPHEUS_OK)
        orpheus_set_parameter(&inst_gain1, GAIN1_GAIN, &ctl0);

    /* probe1.rms [4] -> matrix1.matrix [2x2]（BULK 槽） */
    static float ctl1[4];   /* 形状生成期已知，静态分配，无 malloc */
    if (orpheus_read_bulk(&inst_probe1, PROBE1_RMS, ctl1, 4) == ORPHEUS_OK)
        orpheus_write_bulk(&inst_matrix1, MATRIX1_MATRIX, ctl1, 4);
}
```

理由：
1. **双路径一致性**：动态路径（runtime 块边界做同样的 get/set）与生成路径语义同构，逐字节一致性测试直接覆盖。全局变量+指针引用在动态路径无对应物，等于一个特性两套语义。
2. **组件零改动、封装不破**：`set_parameter` 仍是参数唯一写入口，范围钳制/校验/`update_policy` 检查天然生效（`restart_required` 天然连不进来）。
3. **反隐含**：全局变量式把数据泄漏到共享状态，写入时机与校验不可见。
4. 若未来需音频速率调制（第 2 期 sidechain），正解是 control 端口挂真实缓冲（显式连线），而非隐藏全局变量。

### 8.2 参数解析：声明为骨、求值为辅，零推断、零编译依赖

**核心立场：编译不产生新事实，只做校验；所有形状编辑期即时可知（反 AWE "编译一次才知道参数"）。**

| 内容 | 方式 | 说明 |
|---|---|---|
| 形状规则 | 声明（manifest `shape: [param:rows, param:cols]`） | 组件作者声明一次，复用端口表达式语法 |
| 具体形状值 | 自动**求值**（非推断） | UI 与 compiler 同一套 `param:` 表达式求值器，改参即时刷新，无需编译 |
| 可否被绑定 | 声明（`bindable: true`） | 作者明确意图，默认不可绑 |
| 类型/形状转换 | 禁止隐式 | 严格相等；需转换则显式放适配组件（`ctl_broadcast`/`ctl_pick`/`ctl_to_int`） |
| 连线目标 | UI 交互层辅助 | 拖到节点上若唯一匹配则自动选中，纯交互便利，无语义推断 |

机制保证：
- 工程 YAML 只存声明（`control_connections` 的 from/to），不存求值结果 → 派生数据不落盘，无陈旧真相。
- 求值确定性：`shape 表达式 + 当前参数 = 具体形状`；UI 端 `resolveExprValue` 与 compiler `_resolve_value` 同语义。连线上看到的 `[2×2]` 即编译期算出的 `[2×2]`。
- 编译退化为纯检查（形状失配/目标不可绑/restart_required），编译不通不影响图上信息完整性。

## 九、双平面语义：回环、反向传播与链路时序

### 9.1 拆成两个平面

| 平面 | 内容 | 时序 | 环 |
|---|---|---|---|
| 签名平面 | shape/采样率/通道数，表达式声明 | 编译期（=编辑期即时）拓扑求值 | 结构性禁止，成环即编译错误 |
| 控制平面 | bindable 参数的值流动 | 块边界两相快照，每链 1 块延迟 | 允许，闭环=延迟反馈环 |

### 9.2 签名平面：结构性无环

- `shape` 表达式只引用**本节点**参数，跨节点依赖不进表达式；
- **bindable 参数禁止 `affects_signature`**（签名参数必为 `restart_required`，与 bindable 要求的 `immediate/smoothed` 互斥）→ 运行期控制链改不动签名，形状编译后即为常量；
- **反向传播**（下游决定上游，如 wav_out 跟随源采样率的现有特判）泛化为编译期有向依赖图的拓扑求值：表达式可声明拉取上游端口签名（如 `upstream:in.sample_rate`）；输入全是编译期常量，即 DAG 约束求解，编辑期即时出结果；成环则 Kahn 拓扑排序余留节点即环，编译报错并列出环链（`a.channels → b.count → a.channels`）。

### 9.3 控制平面：闭环合法，每条链 = 单位延迟

闭环控制（AGC：`level_detect → gain → 音频 → level_detect`）合法且必须支持。语义：**每条控制链自带 1 块延迟**（单位延迟环节），闭环天然含延迟，无代数环问题——块 k 的目标值取决于源在块 k-1 的值，行为确定。

### 9.4 链路形式：快照式两相提交（解决 get/set 串链的顺序敏感）

裸 get/set 逐链执行时，多跳链（A→B→C）的传播延迟取决于生成代码行序——这是"不方便串成链路"的本质。解法是用语义消掉顺序，**先全读、后全写**：

```c
/* 控制帧：上一块边界的快照（可经 PROBE_JSON 整体观测，便于调试） */
typedef struct { float level1_level; float probe1_rms[4]; } CtlFrame;
static CtlFrame cf;

static void control_tick(void) {          /* 块边界调用一次 */
    /* 第一相：读 —— 全部源槽 → 快照 */
    orpheus_get_parameter(&inst_level1, LEVEL1_LEVEL, &cf.level1_level);
    orpheus_read_bulk(&inst_probe1, PROBE1_RMS, cf.probe1_rms, 4);
    /* 第二相：写 —— 快照 → 全部目标 */
    orpheus_set_parameter(&inst_gain1, GAIN1_GAIN, &cf.level1_level);
    orpheus_write_bulk(&inst_matrix1, MATRIX1_MATRIX, cf.probe1_rms, 4);
}
```

性质：
1. **顺序无关**：行序不构成语义，链路任意串联/扇出，A→B→C 确定两块延迟；
2. **闭环自动良定义**：每条边都是单位延迟，无需环检测（最多提示「闭环含 N 个单位延迟」）；
3. **双路径一致 trivial**：动态路径 runtime 做同样的两相操作；
4. **手工风**：直线代码、无表驱动间接层、静态分配、无 malloc。

### 9.5 零延迟链（可选演进）

默认 1 块延迟覆盖绝大多数场景。确需同块穿透时**显式声明** `latency: 0`：compiler 对零延迟边子图拓扑排序生成顺序代码，并对零延迟边子图查环报错（等同 Simulink direct-feedthrough 分析）。默认有延迟、零延迟须声明，符合"自动化但不隐含"。

---

## 十、实施状态（已落地，2026-08）

第 0 期与第 1 期已完整实现（框架 → 界面 → 代码生成全链路）：

- **schema/registry**：parameters 新增 `bindable`/`control_source`/`shape`（component_manifest.schema.json）；工程 schema 新增顶层 `control_connections`；`/api/components` 经 `parameters` 原样透传，UI 零后端改动拿到新字段。
- **compiler**：`_validate_control_links()` 全量中文校验（存在性/bindable/control_source/update_policy/非 affects_signature/类型严格相同/shape 两端求值后严格相等），并拒绝**重复目标**（同一参数被多条链驱动——两相快照下同块两写属模糊行为），产出 `plan.control_links`；subgraph flatten 与 resolve.py（alter）同步处理控制连接。
- **runtime 动态路径**：`Runtime::control_tick`（runtime.cpp）挂载于 `process_block` 节点循环之后、块计数之前——rt_host 按 block_size 分块驱动，tick 恰为每图块一次；两相快照、字符串缓冲 load_plan 预分配（256B），process 路径零分配。
- **代码生成路径**：generator 在 `orpheus_generated_process` 末尾生成等价 `control_tick()`（快照静态数组 + 直线 get/set 代码，每链一行 `src [shape] -> dst` 注释）；win 宿主/dsp 骨架/文件时钟共用同一挂载点，模板零改动。
- **UI**：`ui/src/ControlEdge.js`（虚线动画 + 形状 label + 失配红）；OrpheusNode 控制区（`ctl:<param>` 方形橙 handle，开关门控）；App.js 工具栏开关（localStorage `orpheus.showControlLinks`，默认关）、onConnect 全量校验、参数改动后失配边标红保留；graphUtils `resolveShape`/`shapeText`/`shapeEquals` 与 `control_connections` 往返序列化。
- **试点组件**：gain.gain_db（bindable）、level_detect.level / probe_rms.rms（control_source）、matrix_mul.matrix（shape=[param:rows, param:cols]）。
- **测试**：`test_control_links.py` 16 项（含重复目标拒绝、扇出、多跳中继）+ `test_server.py` compile API 2 项（非法链 400 中文报错、合法链响应含 control_links）+ `ui/src/graphUtils.test.js`（jest 11 项：resolveShape/shapeText/shapeEquals/控制 handle/doc↔views 往返）+ `test_generated_run_matches_dynamic_run_with_control_link`（双路径逐字节一致 + 无链基线对照证明控制生效）；示例 `examples/control_link_demo.yaml`（电平闭环 AGC）。compile API 响应新增 `control_links` 字段（增量兼容）。

**本期边界（设计内限制）**：运行期仅执行 float/int/bool 标量 + string 透传；count>1 数值数组链与 bulk 槽投递仅编译期校验、运行期跳过（prepare 时中文提示 / 生成侧注释）；跨子图边界的控制连接不支持（校验报错）；零延迟链（`latency: 0`）未实现。第 2 期（control 速率缓冲端口 sidechain）未动工。
