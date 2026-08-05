# 组件自定义 UI 控件机制设计（Component UI Extension）

> 状态：设计草案（v0.1）。目标：让组件可以自定义其外观与行为（参数显示为旋钮/滑动条、示波器直接在节点上显示波形），且保持**可选、不耦合、不嵌入核心**。

---

## 0.5 实施状态（v1，2026-08-05）

**已落地：probe_waveform 波形显示（第一条完整闭环）**

- C 组件：`probe_waveform` 增加 1024 帧环形缓冲（取第 0 通道）+ `waveform` STRING readback 参数（`get_param` 在非实时线程编码为 JSON 数组）。
- 数据通路：宿主（`rt_host` / `orpheus_runtime`）对 STRING 型 readback 参数输出 `PROBE_JSON <node> <param> <json>`；`rt.py` / `app.py` 解析为结构化值（数组/对象/数字）。
- UI：`nodeWidgets.js` 新增 `ScopeWidget`（canvas 示波器）并注册到 `orpheus.builtin.probe_waveform`；参数面板隐藏显示型 readback 参数（`readback && !affects_signature`）。
- 示例与测试：`examples/probe_waveform_scope.yaml` + e2e 测试断言回读 1024 点浮点数组。

**与草案的偏差（有意简化，保持不耦合）**

1. **注册表按组件 id 键控**（沿用 probe_rms/peak 既有模式），manifest 的 `ui:` 字段暂未使用——渲染定制零核心改动；
2. **PROBE_JSON 判定规则简化为「STRING readback → PROBE_JSON」**，未引入 `probe_json` manifest 标记（当前无其他 STRING readback 参数，向后兼容）；
3. `knob` 参数控件与 manifest `ui` 声明列为 v1.1 后续，不在本次范围。

---

## 0. 背景与现状诊断

### 0.1 为什么 `probe_waveform` 没有效果显示

排查结论：不是单点 UI bug，而是**整条链路缺三环**。

1. **组件没有采集数据**：`probe_waveform` 的实现只是直通（`memcpy` 输入到输出），state 里没有任何波形缓冲，也**没有 readback 参数**（对比 `probe_rms` 有 `rms`、`probe_peak` 有 `peak`）。宿主只上报 `readback && !affects_signature` 的参数，所以上游根本没有波形数据可显示。
2. **协议传不了复合数据**：探针协议是文本行 `PROBE <node> <param> <value>`，后端用 `line.split()` 解析、要求恰好 4 段。波形数组（几百个浮点）既放不进单 token，也带空格会被拆断。
3. **UI 没有注册渲染**：`NODE_WIDGETS` 注册表只有 `probe_rms` / `probe_peak`，没有 `probe_waveform` 条目，即使有数据也只会显示默认节点。

### 0.2 现状已有的定制机制（可复用的范式）

| 机制 | 位置 | 现状 |
|---|---|---|
| 参数控件注册表 | `ui/src/widgets.js` | manifest 参数级 `widget:` hint → React 控件（number/text/slider/select/checkbox/file） |
| 节点本体注册表 | `ui/src/nodeWidgets.js` | 组件 id → 画布节点内部渲染（probe_rms/peak 电平条已工作） |
| 探针数据通路 | readback 参数 + `PROBE` 行 + `data.probe` 注入 | 单值 float/int 可用；复合数据不可用 |

关键事实：现有 `widgets.js` / `nodeWidgets.js` 已经是「manifest 软声明 + 前端注册表」模式——C ABI、plan、编译、Runtime、代码生成**完全不感知** UI。本设计是把这个范式补齐数据通路并规范化，而不是发明新架构。

---

## 1. 设计目标与非目标

### 目标

- 组件作者可用「声明 + 注册」定制两类东西：
  - **参数控件**：如增益显示/调节为旋钮、滑动条、自定义交互；
  - **节点本体**：如示波器节点直接内嵌波形显示、电平表、自定义面板。
- 探针数据通路支持多值/复合数据（波形采样数组）。
- 完全可选：没有 UI 扩展的组件，功能与默认渲染不受任何影响。
- 改动集中在 UI 层与探针数据薄层，核心契约零侵入。

### 非目标（明确不做）

- **不进核心契约**：C ABI、Execution Plan、编译 pass、Runtime、代码生成均不感知 UI 声明。
- **不做组件包内动态加载 JS**（`components/.../ui/*.js` 由前端动态 import 执行）：有信任与沙箱问题，留作未来选项。
- **不引入重型图表库**：波形用原生 canvas/SVG 轻量绘制；chart.js 等只作可选后续。
- **不做组件 → UI 专属数据格式**：UI 数据一律走 readback 通用通路。

---

## 2. 设计原则

1. **软引用**：manifest 中的 UI 声明只是 hint。前端查不到注册表条目 → 回退默认渲染 + `console.warn`，绝不报错、绝不影响编译与运行。
2. **注册表是唯一 UI 契约**：渲染能力 = 前端两个注册表（`widgets.js`、`nodeWidgets.js`）。C/Python 侧零改动。
3. **数据与渲染分离**：readback/probe 通路只负责把数据送到 `data.probe`；控件只是消费方。
4. **向后兼容**：新增字段全是可选项；新后端解析旧 `PROBE` 行不变，旧 UI 忽略新字段。

---

## 3. 机制设计

### 3.1 声明层：manifest 新增可选 `ui` 元数据

在 `component.yaml` 顶层新增 `ui:` 字段（当前 JSON Schema 未开 `additionalProperties: false`，附加键天然放行；后续可选在 schema 中正式声明以获得文档化校验）：

```yaml
ui:
  node_view: scope            # 画布节点本体控件 id；前端注册表查表，缺省回退默认节点
  parameter_widgets:          # 显式声明参数控件（与参数级 widget: 字段等价，可二选一）
    gain_db: knob
```

参数级已有 `widget:` 枚举（number/text/slider/select/checkbox/file），设计上：

- 保持参数级 `widget:` 为**推荐**声明点（改动最小）；
- `ui.parameter_widgets` 用于集中声明、覆盖或表达"参数→控件"映射；
- `ui.node_view` 为新增的节点级声明，与 `nodeWidgets.js` 注册表一一对应；
- 所有 UI 声明止步于 manifest，**不进入 plan**。

消费方只有前端；`registry/compiler/builder/generator/runtime` 不读取该字段。

### 3.2 渲染层：扩展现有两个注册表

**参数控件注册表 `widgets.js`**（接口不变：`{schema, value, onChange, disabled, ctx}`）：

- 新增 `knob`：SVG/canvas 旋钮（垂直拖拽或弧形拖拽改值，显示单位与当前值），复用现有 `range/unit` 元数据。
- 已有 `slider` 可直接作为"滑动条"控件使用。

**节点本体注册表 `nodeWidgets.js`**（接口从 `{data}` 扩展为可选 `{data, updateParam, ctx, probe}`）：

- 新增 `scope`：canvas 示波器（消费 `data.probe.waveform` 数组，绘制波形网格）。
- 新增/整理 `level_meter`、`peak_meter` 等，把现有 probe_rms/peak 电平条规范化。
- 节点本体控件可渲染交互元素（旋钮、按钮），通过可选 `updateParam` 回调走现有 `SET` 实时通路（rt 会话已支持运行中调参即时生效）。
- 兼容策略：`OrpheusNode` 渲染时按需传 `updateParam`，老控件只解构 `data` 不受影响。

### 3.3 数据层：探针通路最小扩展（PROBE_JSON）

现状协议：`PROBE <node> <param> <value>`（单 token，200ms 轮询 / 离线一次性，rt.py 用 `split()` 解析）。

**推荐方案：新增 `PROBE_JSON` 行**（保持旧格式完全兼容）：

```
PROBE_JSON <node> <param> <json>
```

- 后端 `rt.py` / `app.py`：对 `PROBE_JSON` 前缀行整行 `json.loads` 解析为 number/string/array/object；旧 `PROBE` 行逻辑不动。
- 宿主 C++（`rt_host.cpp` / `main.cpp` 的 `report_probes`）：标量参数仍走旧格式；复合值（manifest 标记 `probe_json: true` 或组件返回的 JSON 字符串）走 `PROBE_JSON`。
- 限流与上限：单行 ≤ 64KB，超限丢弃并计数（Debug 语义：可丢弃，不阻塞实时线程）；轮询周期维持 200ms 可配置。
- 前端：`data.probe[param]` 直接获得数组/对象，`scope` 控件消费。

备选（更保守，不推荐）：复合值 base64 编码塞进旧 `PROBE` 行单 token。缺点：不可读、不通用，仅省后端两行解析。

### 3.4 组件侧改造（以 `probe_waveform` 为例，即修复方案）

1. **采集**：state 增加固定容量环形缓冲（如 1024 帧 × 1 通道；窗口长度/通道可选参数）。`process` 内 `memcpy` 写入——实时安全、零分配。
2. **回读**：`get_param("waveform")` 在**非实时线程**（probe 上报线程）把缓冲编码为 JSON 数组字符串（如 `[0.1,-0.2,...]`），返回 `ORPHEUS_VALUE_STRING`；红线只约束 `process`，此处安全。
3. **manifest**：`waveform` 参数 `readback: true`、`probe_json: true`；顶层 `ui.node_view: scope`。
4. **离线路径**：`main.cpp` 同样输出 `PROBE_JSON` 行，后端解析后注入 `data.probe`，离线跑完也能显示波形。

动态加载与代码生成两条路径共享同一 `get_param`，双路径一致性天然保持。

---

## 4. 端到端示例

**示波器**：`probe_waveform` 组件采集 → `get_param` 输出 JSON 数组 → 宿主 `PROBE_JSON` 行 → 后端解析 → `data.probe.waveform` → `scope` 控件在画布节点内实时画波形。

**旋钮**：`gain` 的 `gain_db` 参数声明 `widget: knob`（或 `ui.parameter_widgets`）→ 参数面板与（可选）节点本体渲染旋钮 → 拖拽 → `updateParam` → `SET gain_db` 实时通路即时生效。

---

## 5. 兼容性与降级

| 场景 | 行为 |
|---|---|
| 老组件无 `ui` 字段 | 默认渲染，现状不变 |
| 新组件 `ui` 指向未注册 id | 默认渲染 + `console.warn` |
| `PROBE_JSON` 行遇到旧后端 | 按日志行显示，无害 |
| 新后端解析旧 `PROBE` 行 | 完全兼容 |
| 组件无 readback | 控件显示占位文案（现有做法） |

---

## 6. 实施清单（按需拆分，可先做 1+4 修复 probe_waveform）

1. C：`probe_waveform` 环形缓冲 + `waveform` readback + manifest（`probe_json`、`ui.node_view`）；schema 可选显式声明 `ui`/`probe_json`。
2. C++：`report_probes`（rt_host + main）按参数输出 `PROBE_JSON`。
3. Python：`rt.py` / `app.py` 解析 `PROBE_JSON`（`/api/components` 已透传 manifest，无需改）。
4. UI：`widgets.js` 加 `knob`；`nodeWidgets.js` 加 `scope`；`OrpheusNode` 可选传 `updateParam`/`ctx`。
5. 测试：后端 `PROBE_JSON` 解析单测；e2e 验证离线/实时图中 probe_waveform 显示波形；既有 29 项测试不回归。
6. 文档：README/HOW 与 SKILL references 更新（若有）。

---

## 7. 风险与边界

- **带宽**：每 200ms 上报 1024 浮点 ≈ 20KB/s/节点，可接受；窗口与通道数可配以控制开销。
- **实时安全**：波形写入在 `process` 内仅为定长 `memcpy`；编码发生在非实时线程。
- **不耦合保障**：UI 声明止步于 manifest；plan 保持纯净；组件 C 代码不感知任何 UI 概念。
- **未来选项**（不进 v1）：组件包自带 UI 资源动态加载；WebSocket/SSE 探针推送（替代轮询）；节点内直接操作参数的通用能力。
