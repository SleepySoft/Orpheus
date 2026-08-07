# Orpheus 组件资源注册与寻址设计（草案）

> 状态：设计草案（尚未实现）。本文记录 2026-08-06 关于"组件如何向 Runtime 暴露配置参数、调音参数、探针"的讨论结论，作为后续 ABI v2 与组件改造的输入。

---

## 1. 背景与现状问题

当前实现是"被动描述 + 字符串派发"：

- 组件在 C 中声明静态 `OrpheusParameter[]` 描述符，同时在 `component.yaml` 中再维护一份（含 widget/options）。两个事实来源，无交叉校验，漂移无人发现。
- 实例状态由 `create()` 中 `calloc` 分配的不透明 `void*` 持有，Runtime 只知 `state_size`，不知内部布局。
- 参数访问必须回绕到 `set_parameter/get_parameter` 回调，组件内部手写 `strcmp` 分发（样板代码、易拼错、O(n) 派发）。
- 探针被实现为 `readback: true` 的参数：波形探针在非实时线程把环形缓冲手搓成 JSON 字符串，宿主靠 `component.find(".probe")` 猜测探针节点。
- 没有任何数值 ID：协议、UI、代码生成全靠 `node_id + param_id` 字符串。

核心问题：**Runtime 拿不到组件数据的地址与类型，所有访问被迫回绕到回调，配置/调音/探针混在同一模型里，无法统一寻址与检测。**

---

## 2. 设计目标与边界

### 目标

1. 组件以"结构体成员"方式自行组织资源（类成员/结构体成员模式），通过一行宏把地址、类型、说明注册给 Runtime。
2. Runtime 对全部注册槽的地址与布局全知，可统一做边界检测与内存状态检查。
3. ID 由外部确定的部分（组件 ID、DSP Core 等位域）与内部确定的部分（实例号、槽号）分离。
4. 动态加载路径与代码生成路径逐字节一致（仓库红线）。
5. 实时路径零分配、零阻塞（仓库红线）。

### 非目标（本设计不做）

- 不替代 manifest 的编译期职责：影响签名的参数（`affects_signature`）仍由 manifest + plan 决定，注册表只做运行期寻址。
- 不拦截组件对自身字段的直接写（C 语义下不可行），只做注册期/访问期/调试期防线。
- 不改变实时会话文本协议（保留兼容别名）。

---

## 3. 核心概念：资源槽（Resource Slot）

把配置/调音/探针归一为**可寻址数据项**，用 kind 区分语义：

| Kind | 方向 | 用途 | 典型 |
|---|---|---|---|
| `SETTING` | 读写 | 调音参数 | gain_db、fc、q、smoothing_ms |
| `COMMAND` | 只写 + 确认 | 一次性命令 | reset、bypass、切换 preset |
| `BULK` | 读写 | 大块数据（双 bank） | FIR 系数、查找表 |
| `PROBE` | 组件写 / Runtime 读 | 观测 | rms、waveform、spectrum bins |
| `STATE` | 只读 | 调试内部状态 | head、block_counter |

配置参数（channels、type 等影响签名的）**不进注册表做寻址**，它们是编译期事实，走 manifest + plan + `OrpheusConfig`；加载时登记为只读槽供 UI/协议查询。

### 3.1 两类组件形态

| 形态 | 本质 | 现状 |
|---|---|---|
| 原生实现/库组件（Type A） | 固定的 C/C++ 代码单元，可互相引用（deps），最终是一份确定的代码 | 现有组件即此形态，完全支持 |
| UI 组合子组件（Type B） | 工程文件描述引用图（`sub:`），可能含代码也可能纯描述 | 纯描述型已支持（`flatten_project` + UI 框选包装/双击编辑）；**含代码的封装型复合组件为必要但未实现的方向** |

图级复合组件运行期 flatten 为叶子原子实例；"子组件数据注册"最终落在两类地方：叶子自身（若它需要外部数据入口则自己注册），或宿主聚合组件（代理注册嵌入的子块）。

### 3.2 两类数据注册：注册是可选能力，职责上移

- **基础算法（FIR/IIR/Biquad 等）**：注册是"能力"而非义务——它们是库代码，没有外部身份，不需要 `register_slots`。
- **基于基础算法的完整功能组件**：宿主**代理注册**嵌入子算法的 buffer（系数/查表走 BULK 直写），子算法自身不注册。

推论：

1. **公开状态结构体 ≠ 注册能力**：基础算法不实现 `register_slots`，但为了可嵌入性仍需公开状态结构体（宿主内联它、算偏移的前提）。
2. **代理注册用子块字段表**（编译期元数据）+ `ORPHEUS_REG_BLOCK_ARRAY`，避免宿主手写 `offsetof` 散落。
3. **代理注册依赖 deps 复用模型**（Type A 互相引用：声明/悬空符号/递归闭包）。
4. **BULK 槽需要双 bank 原子提交语义**（先写 shadow、边界统一切换）。

---

## 4. 内存模型：实例块 + 注册区/工作区

### 4.1 实例状态块

每实例一块连续、对齐内存：

```
实例状态块（每实例一块，连续、对齐）
┌─────────────────────────────────────┐
│ GainState                          │
│  ├─ 0x00  gain_linear  (f32)       │ ← 注册偏移 0x00
│  ├─ 0x04  target_linear (f32)      │ ← 注册偏移 0x04
│  ├─ 0x08  smoothing_coeff (f32)    │
│  └─ 0x0C  channels (u32)           │ ← 注册偏移 0x0C
└─────────────────────────────────────┘
```

- 动态路径：Runtime 按描述符 `state_size` 分配（加载期一次性，不违反实时红线）。
- 生成路径：`static uint8_t g_state_<node>[size] __attribute__((aligned(8)))`。
- 组件 `create` 不再 `calloc`，直接 `*state = block;`。
- 槽级注册携带显式 `offset/size/type`，供边界检测与交叉校验；顶层布局不解释内容。

### 4.2 注册区与工作区

| 区域 | 内容 | Runtime 感知 |
|---|---|---|
| 注册区（可寻址） | 参数/探针/Bulk，含子块公开数据 | 全知：偏移、span、类型、方向 |
| 工作区（私有） | 系数、延迟线、中间状态 | 不感知布局，按整块 canary 保护 |

建议组件把可寻址数据独立成子结构体（如 `BiquadPub`），与内部成员分开，便于紧凑与批量扫描。

### 4.3 类型级偏移表

同类型所有实例布局相同，因此偏移表是**类型级**的：

- 注册函数跑一次，产出 `SlotMap[type] = {slot_key, kind, type, offset, span, count, flags, meta}`。
- 每个实例只有 `base_ptr` 不同，访问 = `base + type_map.offset`。
- "Runtime 全知"的代价是每组件类型一张偏移表，实例再多也不涨内存。

### 4.4 统一内存分配：拼接/切片模型（演进结论）

顶层统一分配**只保证连续性与对齐，不关心内容**。布局知识由 C 编译器承担，而非运行时计算：

**生成路径——按数量和结构直接拼接组件的数据结构体**：

```c
/* generated main.c：整个图的状态 = 一个拼接结构体 */
static struct {
    GainState    gain_1;    /* 实例 1 */
    GainState    gain_2;    /* 实例 2 */
    EqBankState  eq_1;      /* 聚合组件：内部内联 BiquadState bq[10] */
} g_arena;
```

- 大小、对齐、偏移全部由 C 编译器计算，顶层不需要任何 size/align 元数据。
- **递归 = 结构体内联**：聚合组件的结构体按值包含子块结构体（如 `BiquadState bq[10]`），子块数据区天然连续；"上层构造下层数据区"发生在类型系统（C 语言）中，而非运行时代码。
- 实例 state 指针 = `&g_arena.<成员>`，偏移相对切片基址，槽注册逻辑不变。

**动态路径——按描述符切片**：

```
arena = [实例 1 片][实例 2 片][实例 3 片]...
每片大小 = descriptor.state_size（组件自己声明，加载时 get_descriptor 可得）
拼接规则 = plan 节点顺序 + 每片按其对齐对齐
```

顶层只做"按顺序摆片 + 对齐"，不解释内容。两条路径共享同一条拼接规则（顺序 + 对齐），一致性测试断言两路偏移一致。

**前提与代价**：

1. **状态结构体必须公开**：移入组件 `include/` 头文件（如 `orpheus_gain.h` 声明 `GainState`），manifest 声明 `state_type: GainState`，生成器才能拼接。公开头文件同时服务代码生成与组件间代码复用（复用需要声明）。
2. **状态大小必须编译期固定**：数组一律固定上限（`float z[MAX_CHANNELS]`），动态大小状态不被支持（与签名参数 restart_required 同一纪律）。
3. 不需要 per-field/child 元数据提取；`descriptor.state_size` 是动态路径唯一需要的大小，与 manifest 的 `memory.state_size` 无关。

**边界检测落法**：切片边界 = `state_size`（组件自报）；槽注册校验 `offset + span <= state_size`；canary 放切片间与 arena 两端。

**局部开发**：单组件单测 = 一块栈 buffer；局部组合 = 声明一个结构体；整图 = 引擎。三者在布局上天然一致，因为 C 编译器就是布局算法——无需"布局算法共享库"，也无"过于依赖代码生成"的问题。

---

## 5. 注册 API

### 5.1 槽描述与注册器（ABI v2，接口表尾部追加）

```c
typedef struct {
    OrpheusSlotKind kind;
    const char* key;             /* 稳定逻辑键，与 manifest 参数 id 对齐 */
    const char* name;            /* 中文显示名 */
    OrpheusValueType type;
    size_t offset;               /* 实例块内偏移 */
    size_t size;                 /* 元素字节数 */
    uint32_t count;              /* 数组长度，1=标量 */
    float min_f32, max_f32; int32_t min_i32, max_i32;
    const char* unit;
    OrpheusUpdatePolicy update_policy;
    uint32_t flags;              /* persistent / readback / affects_signature / alias */
} OrpheusSlotInfo;

typedef struct {
    OrpheusSlotId (*add)(void* ctx, const OrpheusSlotInfo* info);
    int (*update)(void* ctx, OrpheusSlotId id, const OrpheusSlotInfo* info);
} OrpheusRegistry;

/* 组件接口尾部追加（老 DLL 为 NULL，走 v1 fallback） */
int (*register_slots)(void* state, const OrpheusRegistry* reg);
```

### 5.2 一行注册宏

```c
#define ORPHEUS_REG_SLOT(reg, state_ptr, field, kind, key, name, type_, ...) \
    orpheus_reg_add((reg), &(OrpheusSlotInfo){ .kind=(kind), .key=(key), .name=(name), \
        .type=(type_), \
        .offset=(size_t)((char*)&((state_ptr)->field) - (char*)(state_ptr)), \
        .size=sizeof((state_ptr)->field), __VA_ARGS__ })
```

组件侧示例：

```c
static int gain_register_slots(void* state, const OrpheusRegistry* reg) {
    GainState* s = (GainState*)state;
    ORPHEUS_REG_SLOT(reg, s, gain_db, ORPHEUS_SLOT_SETTING, "gain_db", "增益",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-96.0f, .max_f32=24.0f,
                     .unit="dB", .update_policy=ORPHEUS_UPDATE_SMOOTHED);
    ORPHEUS_REG_SLOT(reg, s, rms, ORPHEUS_SLOT_PROBE, "rms", "RMS",
                     ORPHEUS_VALUE_FLOAT, .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}
```

宏自动用 `(char*)&s->field - (char*)s` 计算偏移，作者无需手写 `offsetof`。探针与参数同构注册——**探针不再是"参数"**。

### 5.3 子块字段表 + 块数组宏（聚合组件）

子块类型的字段描述表定义在子块自身旁边，父组件一行循环注册：

```c
#define ORPHEUS_FIELD(type_, member, kind_, key_, ...) \
    { .kind=(kind_), .key=(key_), \
      .offset=offsetof(type_, member), \
      .size=sizeof(((type_*)0)->member), __VA_ARGS__ }

static const OrpheusSlotField g_bq_fields[] = {
    ORPHEUS_FIELD(BiquadState, pub.fc, ORPHEUS_SLOT_SETTING, "fc", .unit="Hz"),
    ORPHEUS_FIELD(BiquadState, pub.q,  ORPHEUS_SLOT_SETTING, "q"),
    ORPHEUS_FIELD(BiquadState, pub.rms, ORPHEUS_SLOT_PROBE,   "rms"),
};

/* 父组件：注册整块子数组，合成键如 "bq[3].fc" */
ORPHEUS_REG_BLOCK_ARRAY(reg, s, bq, 10, g_bq_fields);
```

展开逻辑：对每个 i、每个字段，计算
`offset = offsetof(Parent, bq) + i*sizeof(BiquadState) + offsetof(BiquadState, 字段)`，
以 `"bq[i]." + key` 合成键注册。Runtime 收到的仍是扁平叶子槽表，层级只活在 slot_key 命名空间里；嵌套聚合递归成立（对应图展开的递归 flatten 思路）。

---

## 6. ID 与寻址

### 6.1 两层映射：逻辑键（稳定） + 物理表（易变）

| 层 | 内容 | 稳定性 |
|---|---|---|
| 逻辑键（对外） | `(node, slot_key)`，slot_key 为稳定字符串（`"gain_db"`、`"bq[3].fc"`） | 组件版本契约，作者维护 |
| 物理层（对内） | `SlotMap[type]`：槽序号/偏移/span/类型，加载时由注册重建 | 每次加载重新生成 |

- slot_key 集合 = manifest 参数 id + 探针 id + bulk id（同一份清单）。加载时注册结果与 manifest 交叉校验（键集合、类型、范围一致），根治"双份元数据漂移"。
- 持久化、UI、预设、协议引用一律走逻辑键；数字 ID 只在会话内有效。
- 变化规则：追加槽 = 兼容；改名/删槽 = 必须升组件版本。

### 6.2 64 位 ID 布局（对外稳定 ID = 查表键）

```
63..60  version  = 1
59..56  core     (4b, 预留 DSP Core/多 Task 归属，当前恒 0)   ← 外部确定
55..48  kind     (8b, SETTING/COMMAND/BULK/PROBE/STATE)
47..32  type_id  (16b, 组件类型注册序号，按组件 id 排序分配)    ← 外部确定
31..16  instance_id (16b, 该类型下的实例序号，按 plan 顺序)    ← 内部确定
15..0   slot_id  (16b, 实例内槽注册序号)                      ← 内部确定
```

### 6.3 结论：查表，偏移不入 ID

1. 边界检测需要每槽元数据（span/类型/方向），元数据天然构成表；表存在后，偏移入 ID 只省一次查表，意义为零。
2. 偏移随结构体演化/换编译器而漂移，入 ID 会使同一逻辑资源跨版本/跨编译器 ID 不同，破坏协议/持久化/测试断言。
3. 表项缓存解析结果：加载时 `base + offset` 解析为指针缓存，访问一次解引用，性能与偏移入 ID 相同。
4. 调试需要看偏移时，`resolve(id)` 返回 `{base, offset, span}`，日志附 `@0x2C` 展示即可。

可选：进程内 32 位快速句柄 `instance<<16 | offset`，仅进程内、不跨进程、不持久化、不对外。

---

## 7. 边界检测

按访问方向分三个穿越点：

| 穿越点 | 写入方 | 可检测性 | 手段 |
|---|---|---|---|
| 注册期 | 组件 → Runtime | 完全可检 | `reg->add` 校验，fail-fast |
| 访问期 | Runtime/控制线程 → 组件槽 | 完全可检 | 访问 API 校验，无部分写 |
| 组件内部直写 | 组件 process | 不可拦截 | 静态断言 + canary + ASan |

### 7.1 注册期校验（`reg->add`）

```
1. key 唯一；kind/type/count 合法（count >= 1）
2. offset + count*size <= block_size                       ← 上下边界（整块越界）
3. offset % 字段对齐 == 0                                   ← 对齐（ARM 等严格平台）
4. 与既有槽不重叠，除非显式 ORPHEUS_SLOT_ALIAS 标志         ← 内部边界
```

`block_size` 以描述符 `state_size` 为权威；注册只添加元数据，不改变分配权。

### 7.2 访问期校验（每次 SET/GET/BULK/探针读取）

```c
OrpheusResult orpheus_slot_read (OrpheusRuntime* rt, OrpheusSlotId id,
                                 void* out, size_t out_cap, size_t* written);
OrpheusResult orpheus_slot_write(OrpheusRuntime* rt, OrpheusSlotId id,
                                 const void* in, size_t in_size);
```

校验顺序（全部通过才执行一次 memcpy，绝不部分写）：

```
1. 解包 ID 位域，core/type/instance/slot 索引落在表维度内
2. 槽存在，方向与 kind 匹配（PROBE 不可写、COMMAND 不可读）
3. 类型匹配（f32/i32/bool/string/bulk）
4. in_size == count * elem_size（数组拒绝截断/多写）
5. offset + count*elem_size <= block_size                   ← 上下边界（防御纵深）
6. 通过 → memcpy
```

可选：SETTING 槽按 `range` 元数据做"拒绝越界 vs 自动钳制"策略位。所有校验在控制/探针线程或加载期执行，**实时路径零开销**。

### 7.3 调试期防线

- 编译期静态断言：`_Static_assert(offsetof(State, field) == 注册偏移)`，防布局漂移；数组容量用 `sizeof(field)/sizeof(elem)` 宏。
- 调试 canary：debug 构建在实例块首尾放魔数，`prepare`/`reset`/`process` 边界校验，探针线程可定期扫描（内存健康检查）。
- ASan：测试构建开启，抓字段内越界。

---

## 8. 运行时行为

- `set_parameter(id, value)`：查表 → 类型化直写 `base + offset`，无 `strcmp` 派发。需要换算/副作用时槽可带可选 `on_write` 钩子（如 gain_db 的 dB↔线性）。
- `get_parameter`：直接读地址。探针线程遍历注册表的 PROBE 槽（不再 `component.find(".probe")`），标量走 `PROBE`、矢量走 `PROBE_JSON`/二进制块，格式由槽元数据决定。
- 更新策略（immediate/block_boundary/smoothed/transactional）变成框架可做的事：影子值、块边界提交、批量事务由 Runtime 统一实现。
- 加载时交叉校验：注册槽与 manifest 比对 id/type/range/count，不一致报错。
- 运行时维护 `slot_key → 表项` 双向映射，`(node, slot_key)` 与数字 ID 互转。

---

## 9. 代码生成一致性

- 生成 `main.c` 声明静态拼接结构体（`g_arena`）并下发 `state_block`，`create` 直接绑定；生成路径也调用 `register_slots`（占位注册器，流程与动态路径对等，bulk/控制通路启用后可扩展）。一致性由"两路同一份组件源码 + 同一布局规则"保证。
- 顶层布局 = 类型拼接（生成路径）或按 `state_size` 切片（动态路径），不依赖任何大小元数据；槽级注册仍显式携带 offset/size 供边界检测。组件侧 `_Static_assert` 锁"实际偏移 == 注册偏移"，防跨编译器布局漂移。
- 确定性 ID（type 按组件 id 排序、instance 按 plan 顺序、slot 按注册顺序）使动态/生成路径可复算同一套 ID，供测试断言。

---

## 10. 实时安全与并发

- 标量探针：对齐单字 + 原子/volatile 读，RT 线程写、探针线程读无撕裂。
- 矢量探针（波形/频谱）：seqlock（写侧递增序列号）或双缓冲 + 原子切换，探针线程读到一致快照，接受有限陈旧。修复现 probe_waveform 的数据竞争隐患。
- 注册与校验全部发生在非实时路径。

---

## 11. ABI 兼容与迁移路线

1. ABI v2：接口表尾部追加 `register_slots`，`abi_version=2`；Runtime 探测函数指针，NULL → v1 fallback。老 DLL 全兼容。
2. 试点迁移 3 个组件：`gain`（SETTING）、`probe_rms`（PROBE 标量）、`probe_waveform`（PROBE 矢量 + seqlock），删掉各自 `get/set_parameter` 的 strcmp 样板。
3. Python 侧：registry/generator 增加注册表与 manifest 交叉校验；生成 main 输出静态块并调用同一注册函数。
4. 协议：新增 ID 别名映射（`SET <u64id>`），保留旧文本格式；探针上报改走注册表。
5. 最后补 COMMAND/BULK 槽实现（对应 WHAT.md 控制口/Bulk 双 bank）。

---

## 12. 待定 / 开放问题

- 动态数量槽（如 per-channel 探针）：布局随 prepare 变化，需要"prepare 时重建类型级映射 + 槽号稳定性策略"（先按固定最大数组实现）。
- BULK 双 bank 的原子提交语义与注册表如何表达。
- COMMAND 的确认（ack）语义与超时。
- 含代码的封装型复合组件（Type B 带代码）如何建模（manifest `graph + code`?）。
- 子块字段表放 header（C 侧，组件作者维护）还是 manifest（Python 侧可见、生成器可用）。
- （已解决 2026-08-06）生成路径注册器：生成 main 已调用 `register_slots`（占位注册器）；bulk/控制通路启用时在此扩展。
- （已解决 2026-08-06）块大小权威：动态路径 = 描述符 `state_size`（组件自报）；生成路径 = 类型拼接，无需大小元数据。
- 跨编译器（MSVC/MinGW）布局差异的静态断言覆盖范围。

---

## 13. 实证验证与修正（2026-08-06）

用三个例子实际生成代码并双路径运行（动态 vs 生成，逐字节比对）：

1. **原生 v2 组件链**：`wav_in → gain(-6dB) → probe_rms → wav_out`。`g_arena` 为 `GainState gain1; ProbeRmsState mon;`，非 v2 组件（wav_in/out）保持 `create` + calloc。字节一致，探针 `rms=0.1727`。
2. **一层 UI 复合 `sub:fx`**：flatten 后节点 id 为 `fx__g`、`fx__mon`，`g_arena` 按 plan 执行序拼接叶子；探针 id 为 `fx__mon`。字节一致。
3. **两层嵌套 `sub:fx → sub:chain`**：flatten 递归为 `fx__g2`、`fx__c__g1`、`fx__c__mon`、`fx__g3`；探针 `fx__c__mon` 正常。字节一致。

### 修正（推论与实际的差异）

1. **生成路径当前不调用 `register_slots`**（已解决）：生成 main 曾只跑 create/prepare/process/destroy；现已在 create 后调用占位注册器（流程对等，DSP 输出不变）。
2. **两种嵌套是两种代码形态**：图级复合（UI `sub:`）被完全摊平成"多个实例成员"（`g_arena` 里的独立成员）；组件内部子块保留在"单个状态结构体内部"。统一机制成立（类型拼接 + 注册），但不宜表述为"一棵内存树"。
3. **`g_arena` 是实例粒度（plan 级）拼接**；运行期 SlotMap 是类型级偏移表。两者并存：生成期布局在实例，运行期寻址在类型。
4. **节点 id 必须清洗为合法 C 标识符**：实测节点名含 `.`（`my.gain`）会生成非法代码；`_sanitized_node_id` 改为正则替换所有非标识符字符（`my.gain → my_gain`）。
5. **float 参数必须按 manifest 类型下发**：生成路径曾把 `"-6.0"` 当 STRING 导致 0dB 运行（已修）；生成工程 MSVC 需 `/utf-8`（中文槽名/字符串，已修）。

---

## 14. 决策记录

### 2026-08-06

- 采用"结构体成员"方式分配资源：组件定义布局，Runtime 提供每实例一块连续内存；纯静态全局因多实例问题被否决。
- ID 中只有 core/组件类型由外部确定，实例号/槽号内部确定。
- **偏移不入 ID，采用查表**：表项含 offset/span/type/kind/meta，并缓存解析指针。
- 两层映射：对外稳定逻辑键 `(node, slot_key)`，对内易变物理表 `SlotMap[type]`；slot_key 与 manifest 参数清单同源并交叉校验。
- 探针从"readback 参数"升级为独立 PROBE 槽，矢量探针用 seqlock/双缓冲。
- 聚合组件：父结构体内联子块数组（物理连续）+ 子块自带字段表（登记集中）+ 前缀合成键。
- 边界检测三层：注册期 fail-fast、访问期全校验无部分写、调试期静态断言 + canary + ASan。

### 2026-08-06（第二次讨论：统一内存分配简化）

- 顶层统一分配只保证连续性与对齐，不关心内容；布局知识由 C 编译器承担。
- 生成路径 = 按数量和结构拼接组件数据结构体（一个拼接结构体）；动态路径 = 按 `descriptor.state_size` 切片，两路共享同一条拼接规则（plan 顺序 + 对齐）。
- 递归 = 结构体内联：聚合组件结构体按值包含子块结构体，子块数据区天然连续。
- 状态结构体必须公开（进 include 头文件），manifest 声明 `state_type`；状态大小必须编译期固定（数组固定上限）。
- 不需要 per-field/child 元数据提取；`descriptor.state_size` 是动态路径唯一需要的大小。
- 局部开发：C 编译器即布局算法，单测/局部组合/整图布局天然一致。

### 2026-08-06（第三次讨论：两类组件与注册职责）

- 组件形态两类：原生实现/库（Type A，可互相引用）与 UI 组合子组件（Type B，纯描述已支持，含代码封装型未实现但必要）。
- 注册为可选能力：基础算法（FIR/IIR 等）不注册；基于基础算法的完整功能组件由宿主**代理注册**嵌入子算法的 buffer，便于 BULK 直写。
- 公开状态结构体 ≠ 注册能力：可嵌入性要求公开结构体，可寻址性才要求注册。
- 代理注册用子块字段表 + 块数组宏；依赖 deps 复用模型；BULK 双 bank 提交语义待定。

### 2026-08-06（第四次讨论：实证验证与修正）

- 原生链、一层/两层 UI 复合三个例子双路径逐字节一致；flatten 节点 id 规则为 `父节点__子节点` 递归。
- 修正：生成路径当前不调用 register_slots（bulk/控制启用时补）；两种嵌套 = 图级摊平 vs 组件内子块两种代码形态；g_arena 为实例粒度拼接。
- 修复：节点 id 清洗补非标识符字符；float 参数按 manifest 类型下发；生成工程 MSVC 补 /utf-8。

---

## 15. 经验教训（踩坑记录）

1. **构建依赖追踪不可靠**：ninja 可能不感知头文件变化（如 orpheus_abi.h 改动后旧 .obj 未重编）。对策：touch 改动源文件强制重编，或全量 clean（注意 clean-first 直接跑 cmake 会缺 vcvars 环境，须经 `cli build` 或先加载 vcvars）。
2. **生成工程与主构建环境必须一致**：MSVC 下中文 UTF-8 注释只产生 C4819 警告，但**中文 STRING 字面量会编译错**（C2001/C2146）；生成 CMakeLists 必须加 `/utf-8`（主构建已有，生成器已补）。
3. **参数类型必须按 manifest 下发**：plan 中参数可能是 YAML 原字符串（如 `"-6.0"`）；动态路径有 strtof 兜底，生成路径没有——按 manifest 参数类型生成 FLOAT/INT/BOOL，否则组件 prepare 读不到（gain_db 静默变 0dB，输出翻倍）。
4. **节点 id 是自由文本**（用户命名 + 子组件 `__` 展开），生成 C 代码前必须清洗为合法标识符，含 `.`（`my.gain → my_gain`）。
5. **槽路由的边界规则**：标量（count==1）SETTING/PROBE/STATE 直读直写；数组槽（如 waveform）回退回调保持 JSON 编码；类型不匹配（如 SET 发 FLOAT 给 INT 槽）回退回调——保持旧行为，避免行为突变。
6. **ABI 追加字段方向**：接口表/配置结构体只能尾部追加；新版 Runtime 设置新字段，旧组件以 `abi_version` 规避访问新函数指针（读旧 DLL 结构体越界字段是 UB）。反向（旧 Runtime + 新 DLL）在 monorepo 中不支持。
7. **统一 arena 期间的内存浪费是过渡态**：v1 组件仍 calloc，arena 为其预留切片闲置；全部迁移后消除。
8. **manifest deps schema 曾限制 `enum: [miniaudio]`**：组件级 deps 必须放开 schema；生成器按"组件 id ∈ registry"识别组件依赖并递归复制。
9. **组件 CMakeLists 需显式加依赖组件 include 路径**：构建侧暂无 manifest 驱动的自动链接（后续由 builder 生成依赖 cmake）；生成器侧已自动处理。
10. **槽读回语义**：PROBE 槽直读注册内存；SETTING 槽只有 `ORPHEUS_SLOT_DIRECT_WRITE` 才直读/直写，否则回调——避免绕过派生重算（mute/balance/fade 的平滑目标、bass 的派生 dB 读回）。
11. **runtime 必须与组件同代重建**：`cli build` 曾只构建组件，runtime 停留在旧 ABI——迁移后的组件读旧 runtime 的 `OrpheusConfig`（无 `state_block` 字段）属越界读，可致组件行为异常（balance 时好时坏，且为 UB）。已让 `cli build` 全量时顺带构建 `orpheus_runtime`/`orpheus_rt_host`。

---

## 16. 待办清单（Backlog）

- [x] 其余 25 个组件迁移 v2（2026-08-06 完成，28/28：公开状态结构体 + `state_type` + `register_slots` + create/destroy 改法）
- [x] 生成路径注册器：生成 main 调用 `register_slots`（2026-08-06 完成，占位注册器）
- [x] `deps` 复用模型：manifest deps 泛化 + 生成递归复制依赖闭包（2026-08-06 完成，组件级头文件依赖）；构建侧链接闭包与共享库形态仍待
- [ ] 共享 DSP 库 `orpheus.dsp.common`（消重 bass/midrange/treble 重复代码）
- [x] 聚合组件试点：`biquad_bank`（内嵌 2 段 biquad 子块 + 父代理注册子块字段/系数 buffer + `Runtime::write_bulk` 直写闭环，2026-08-06 完成）
- [ ] BULK 双 bank 原子提交语义（单槽直写已可用）
- [x] 探针上报改走注册表（rt_host/offline 宿主遍历 PROBE 槽，替代 `component.find(".probe")`，2026-08-07 完成；顺带修复非 .probe 组件探针漏报：fir.taps、mp3 total_frames）
- [ ] ID/协议层：64 位槽 ID、`slot_key ↔ 数字 ID` 双向映射、`SET <u64id>` 紧凑协议
- [ ] 含代码的封装型复合组件（Type B 带代码）建模
- [ ] 子块字段表放 header vs manifest 的决策
- [ ] 跨编译器布局静态断言覆盖
- [ ] 动态数量槽（per-channel 探针）策略

### 2026-08-06（第五次讨论：经验与待办归档）

- 经验教训与待办清单归档到本文档第 15/16 节；SKILL 同步更新（v2 组件写法、环境要求、生成路径注意事项）。

### 2026-08-06（第六次讨论：全量迁移 + deps + 聚合试点）

- 25 个组件全部迁移 v2（28/28），构建与 44 项测试全过；新增 `biquad_bank` 聚合组件（deps 依赖 biquad、内嵌子块、父代理注册、BULK 直写闭环：默认 rms 0.3409 → 直写系数后 0.1704，越界写入拒绝）。
- 生成路径注册器：生成 main 调用 `register_slots`（占位注册器，流程对等）。
- deps 泛化：schema 放开（不再限 miniaudio）；生成器递归复制依赖闭包源码/头文件并加 include 路径；组件 CMakeLists 需显式加依赖 include（构建侧暂手工）。
- 槽读回语义定案：PROBE 槽直读；SETTING 仅 `DIRECT_WRITE` 槽直读，其余回调（保留派生读回，如 bass gain_db 平滑值）。

### 2026-08-07（第七次讨论：探针发现走注册表 + balance 回归根因）

- 探针发现统一走注册表：Runtime 暴露 `probe_slots()`，rt_host/offline 宿主遍历 PROBE 槽上报，删除 `component.find(".probe")` 猜测；新增 fir.taps 上报回归测试。
- balance"变坏"根因不是机制迁移的 DSP 改动：离线实测 balance=1.0 → L=0/R=0.3535 正确；真正原因是 `cli build` 只建组件、runtime 停留在迁移前 ABI，新组件读旧 `OrpheusConfig.state_block` 越界（UB，时好时坏）。修复 `cli build` 全量构建时顺带重建 runtime/rt_host。
