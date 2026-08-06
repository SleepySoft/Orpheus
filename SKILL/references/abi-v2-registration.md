# ABI v2：资源槽注册与统一内存拼接（组件写法）

> 2026-08-06 落地。设计全文见 `docs/design_registry.md`；试点组件：`gain`、`probe_rms`、`probe_waveform`。

## 一、组件侧要改四件事

### 1. 状态结构体公开

把状态结构体从 `.c` 移进 `include/<name>.h`，并在 `component.yaml` 声明类型名：

```yaml
state_type: GainState
```

生成路径据此按类型拼接 `g_arena`（静态、零 malloc）；动态路径仍按 `descriptor.state_size` 切片。结构体字段布局即契约：数组一律固定上限，改动后所有实例的偏移随编译器自动更新，无需手工维护。

### 2. create / destroy 改为使用下发内存块

```c
static int gain_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(GainState));   /* 旧宿主兼容兜底 */
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int gain_destroy(void* state) {
    (void)state;   /* v2：内存由 Runtime 统一管理，绝不 free */
    return ORPHEUS_OK;
}
```

注意：若 destroy 里有收尾逻辑（如 wav_out 落盘），迁移时要把收尾保留、只去掉 free。

### 3. register_slots：一行宏注册

```c
static int gain_register_slots(void* state, const OrpheusRegistry* reg) {
    GainState* s = (GainState*)state;
    ORPHEUS_REG_SLOT(reg, s, gain_db, ORPHEUS_SLOT_SETTING, "gain_db", "增益",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-96.0f, .max_f32=24.0f, .unit="dB",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_ARRAY(reg, s, buf, 1024, ORPHEUS_SLOT_PROBE, "waveform", "波形",
                      ORPHEUS_VALUE_FLOAT, .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}
```

规则：

- **key 必须与 manifest 参数/探针 id 对齐**（运行期加载时按此交叉校验）。
- 标量（count==1）SETTING/PROBE/STATE 槽由 Runtime 直读直写；**数组槽回退回调**（如 waveform 保持 JSON 编码），不要指望 Runtime 直接序列化。
- `channels` 这类 `restart_required` 参数注册成 SETTING 即可，Runtime 对 restart 参数不直写、回退回调。
- 接口表末尾挂上：`.register_slots = gain_register_slots,`（老组件该字段为 NULL，Runtime 走 v1 回调路径）。

### 4. 参数语义与存储一致

槽直写的是"存储值"。若调音参数带换算（如 gain 的 dB↔线性），把用户可见值直接存字段（如 `s->gain_db`），换算放 `prepare`/`process` 里做，不要让 Runtime 写换算后的内部值。

## 二、Runtime 行为（无需组件操心）

- 加载时整图一块 arena，按 plan 执行序 + `state_size` 切片下发 `config.state_block`。
- `register_slots` 注册进 SlotMap，校验：越界（`offset + count*size > state_size`）、键重复、count==0 → 拒绝。
- `set/get_parameter` 标量槽直读直写；其余回退 `set_parameter`/`get_parameter` 回调。

## 三、自检清单（照抄试点组件写法）

1. `python -m orpheus_core.cli build` 通过。
2. 写一个含该组件的示例工程，动态运行（`/api/projects/{name}/run`）与生成运行（`/run_generated`）输出**逐字节一致**。
3. 探针/readback 回读正常（标量走槽，数组走回调）。
4. 中文名/注释用 UTF-8；别用 PowerShell `Get-Content`/`Set-Content` 改写源码。

## 四、聚合组件与 BULK（biquad_bank 示例）

聚合组件通过 deps 复用基础组件的**公开状态结构体**：

```yaml
deps:
  - orpheus.builtin.biquad
state_type: BiquadBankState
```

```c
typedef struct {
    BiquadState bq[2];   /* 子块内联，物理连续 */
    uint32_t channels;
} BiquadBankState;
```

- 父组件 `register_slots` **代理注册**子块字段（层级键 `fc0`/`q0`/`gain_db0` → `&s->bq[0].fc` 等）。
- 子块系数 buffer（`b0..a2` 连续 5 个 float）注册为 `ORPHEUS_SLOT_BULK`，用 `Runtime::write_bulk(node, key, data, count)` 直写（带容量/边界校验）。
- 生成器按 deps 递归复制依赖组件源码/头文件并加 include 路径；`biquad_bank` 组件的 CMakeLists 需显式加依赖 include 目录。
