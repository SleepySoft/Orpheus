# 模型蒸馏：分析 C/C++ 代码 → 还原滤波器树 → 生成可导入的 Orpheus 工程

> 场景：用户给出公司/目标模型生成的 C/C++ 代码（滤波器实现、状态结构体、参数表），
> agent 需要反向还原出**滤波器构造与参数**，输出**树形可读说明**，并生成能在 Orpheus
> **一键导入**的工程 YAML。参考实现示例：`examples/dsp_model_reference.yaml`。

## 1. 输入与输出

**输入**

- C/C++ 源码路径（文件或目录）。重点文件：
  - `process` 实现：确认信号流（级联/并联/反馈）与逐样本/逐块处理；
  - `prepare`/系数计算：确认滤波器类型与系数公式（如 RBJ cookbook）；
  - 状态结构体（`typedef struct { ... }`）：确认每实例内存、延迟线、通道数组；
  - 参数表（`OrpheusParameter[]` 或等价结构）：确认 id/默认值/范围/单位。

**输出（两份）**

1. **可读树形说明**：工程 YAML 顶层 `model_tree`（嵌套 `children`，每个节点标注
   `filter` 类型与 `params`）+ 一段 Markdown 分析说明（拓扑、公式、参数表、未还原项）。
2. **可导入工程 YAML**：`version` + `graph` + 嵌套 `subcomponents`，全部节点带
   `component` 与 `params`；顶层可同时携带 `model_tree` 与 `metadata.distilled_from`。

## 2. 分析步骤

1. **拓扑**：按连接关系画信号流。级联（x→h1→h2→y）直接串；并联（split→多路→mix）
   用 `deinterleave`/`interleave`（多通道）或 `mixer`（求和）；反馈环（delay 回路）先记录，
   当前 Orpheus 不支持图内反馈环，需拆为任务桥/注释说明。
2. **滤波器原语 → 组件映射**（见第 3 节表）。
3. **参数提取**：id 用 C 代码里的原名（转 snake_case）、默认值、范围、单位；
   平滑参数用 `update_policy: smoothed`，其余影响行为的用 `restart_required`。
4. **签名参数**：`channels`/`sample_rate` 影响端口签名，必须 `restart_required` 且与端口声明一致。
5. **分组为子组件**：按业务模块（前置 EQ / 分频 / 后处理…）组织 `subcomponents`，
   允许嵌套（第 2 层子组件里再放子组件，边界端口 `maps_to` 必须指向**原子节点**端口）。
6. **Bulk 数据**：FIR 系数数组 → `coefficients` 字符串参数并标 `kind: bulk`；
   运行期可写的系数槽（如 biquad 组系数）在 manifest 声明 `bulk_slots`。
7. **探针**：rms/波形/频谱等观测点 → `probe_rms`/`probe_waveform`/`probe_spectrum`
   （readback 参数自动归类为「探针」）。

## 3. 滤波器 → Orpheus 组件映射表

| 源码原语 | Orpheus 组件 | 参数映射 |
|---|---|---|
| 二阶 IIR（RBJ peaking） | `orpheus.builtin.biquad` | `type: peaking, fc, q, gain_db` |
| 二阶 IIR（lowpass/highpass/bandpass/notch/lowshelf/highshelf） | `orpheus.builtin.biquad` | 对应 `type`，fc/q/gain_db |
| N 段 biquad 串联 | `orpheus.builtin.biquad_bank` | `fcN/qN/gain_dbN`（N=0..）；系数 BULK 槽 `bqN.coefs`（5 个 float） |
| FIR 系数数组 | `orpheus.builtin.fir` | `coefficients: "0.5, 0.25, -0.1, ..."`（`kind: bulk`） |
| 增益（dB） | `orpheus.builtin.gain` | `gain_db`（平滑可 `smoothed`） |
| 静音/门限 | `orpheus.builtin.mute` | `mute` 0/1（smoothed）、`ramp_ms` |
| 开关/旁通 | `orpheus.builtin.switch` | `enable` 0/1（smoothed）、`ramp_ms` |
| 峰值限幅 | `orpheus.builtin.limiter` | `threshold_db`、`attack_ms`、`release_ms` |
| 软削波 | `orpheus.builtin.soft_clipper` | `drive_db`（tanh 归一化） |
| 饱和限幅 | `orpheus.builtin.saturation` | `limit`、`soft`（0=硬 1=软） |
| 矩阵乘法 | `orpheus.builtin.matrix_mul` | `rows`/`cols` + `matrix`（BULK，行主序） |
| 窗函数 | `orpheus.builtin.window` | `window_size` + `coefficients`（BULK，每块从头应用） |
| 变化率限幅 | `orpheus.builtin.noise_slew` | `rise_rate`/`fall_rate`（/s） |
| 电平检测 | `orpheus.builtin.level_detect` | `mode`（峰值/RMS）、`attack_ms`、`release_ms` + `level` 探针 |
| 延迟 | `orpheus.builtin.delay` | `delay_ms`、`mix` |
| 多通道拆分/合并（并联各通道） | `orpheus.builtin.deinterleave` / `orpheus.builtin.interleave` | `channels` |
| 并联求和 | `orpheus.builtin.mixer` | `gain0/gain1`（dB） |
| 整数降采样/重缓冲 | `orpheus.builtin.downrate` / `orpheus.builtin.resample` | `factor`/`divisor` |
| 电平/波形观测 | `probe_rms` / `probe_waveform` | readback 探针 |
| 硬件 I/O 占位（嵌入部署，source/sink 手动填充） | `orpheus.builtin.embed_in` / `orpheus.builtin.embed_out` | `channels`/`sample_rate`；`embed_in.underruns` 探针；生成工程 `platform_io.c` 的 USER CODE 段填充 |

## 4. 工程 YAML 骨架

```yaml
version: "0.1.0"
metadata:
  name: company_eq_model
  distilled_from: company_model.c v2.3
sample_rate: 48000
block_size: 128
graph:
  nodes:
    - {id: wav_in, component: orpheus.builtin.wav_in, params: {file_path: "", channels: 2}}
    - {id: front, component: "sub:front"}
    - {id: wav_out, component: orpheus.builtin.wav_out, params: {file_path: outputs/out.wav, channels: 2}}
  connections:
    - {from: "wav_in:out", to: "front:in"}
    - {from: "front:out", to: "wav_out:in"}
subcomponents:
  - id: front
    name: 前置 EQ
    ports:
      - {id: in, direction: input, maps_to: "bq:in"}
      - {id: out, direction: output, maps_to: "gain:out"}
    graph:
      nodes:
        - {id: bq, component: orpheus.builtin.biquad_bank, params: {fc0: 60, q0: 1.4, gain_db0: -2.5, fc1: 1800, q1: 1.0, gain_db1: 1.5, channels: 2}}
        - {id: gain, component: orpheus.builtin.gain, params: {gain_db: -1.0, channels: 2}}
      connections:
        - {from: "bq:out", to: "gain:in"}
model_tree:
  name: Company EQ Model
  children:
    - id: front
      label: 前置 EQ
      children:
        - {id: bq, filter: biquad_bank, params: {stage0: {type: peaking, fc_hz: 60, q: 1.4, gain_db: -2.5}, stage1: {type: peaking, fc_hz: 1800, q: 1.0, gain_db: 1.5}}}
        - {id: gain, filter: gain, params: {gain_db: -1.0}}
```

## 5. 校验清单（提交前逐项过）

- [ ] `version` + `graph` 存在；schema 校验通过（`ProjectLoader` 加载无异常）。
- [ ] 子组件边界 `maps_to` 只指向原子节点；无循环引用；实例引用 `sub:<id>` 均定义。
- [ ] 节点 id 只用 `[A-Za-z0-9_-]`（`.` 等字符会破坏代码生成标识符）。
- [ ] `channels`/`sample_rate` 与端口一致；`restart_required` 标注影响签名的参数。
- [ ] FIR 系数为 `kind: bulk` 参数；运行期系数槽声明 `bulk_slots`（如 biquad_bank）。
- [ ] 探针参数 `readback: true`（自动归入「探针」分类）。
- [ ] `python scripts/parameter_layout.py <project.yaml>`：布局树可读、回写校验通过。
- [ ] `python -m orpheus_core.cli compile <project.yaml>` 编译通过（含设备组件则需时钟源合法）。
- [ ] 有文件输入/输出的图：输入文件放入工程目录，`file_path` 用相对路径。

## 6. 一键导入

- UI：工具栏「⤵ 导入模型」选择 YAML → 输入工程名 → 自动打开工程（画布/参数面板立即可用）。
- API：`POST /api/projects/{name}/distill`，body `{"yaml": "<工程 YAML 文本>"}`；
  顶层未知字段（`model_tree`、`presets` 等）原样保留，编译/运行不受影响。
- **拓扑自动展开**：若 `model_tree.chains` 存在且 graph 是骨架（≤3 节点），导入时自动把每条链
  展开为子模块：流程文本（`A(param) -> B -> ...`）解析成块节点，能映射到内置组件的块用真实组件 id，
  未映射块用占位组件 id（`orpheus.builtin.placeholder`，UI 显示「组件缺失」红标），原始括号参数写入
  节点 `note` 便于查看。此时工程以浏览拓扑为主，替换占位组件前不可编译运行。

## 7. 红线与坑

- 还原的是**信号流与参数**，不是逐行翻译；重复/死代码（未连接的滤波器）应合并或标注「未使用」。
- `process` 内禁止 malloc/锁/IO/printf（实时路径）；不要往还原工程里加日志。
- 系数公式要写明来源（如 RBJ peaking：`A=10^(gain_db/40)`、`alpha=sin(w0)/(2q)`），供人工复核。
- 参数类型：float/int 按 manifest 类型下发；字符串参数（如 `type`）不能当数字解析。
- 采样率：`wav_out.sample_rate` 可留空（编译期自动跟随输入端口）。
- 输出文件一律 UTF-8 无 BOM + LF（Windows PowerShell 默认 GBK 会毁中文，用编辑器/脚本写文件）。
- 未知参数不要臆造：若 C 代码里的参数在 Orpheus 组件里没有对应项，放 `model_tree.params` 注释里说明，而不是塞进工程参数。
