# 模型蒸馏：从外部 C/C++ 证据到可验证 Orpheus 实现

> 场景：用户给出公司/目标模型生成的 C/C++ 代码、参数表或生成工程，agent 需要先建立可追溯结构，再逐模块实现、验证并组装为 Orpheus 工程。小模型可走一键导入兼容路径；复杂多 Runtime/TID 模型必须走 Algorithm IR。

## 0. 先判定任务层级

| 层级 | 允许产物 | 禁止声称 |
|---|---|---|
| 结构索引 | Algorithm IR、模块树、接口、Task、bus、unresolved | 可执行、算法等价 |
| 研究图 | composite + placeholder、标准音频/控制连接、节点说明 | placeholder 已实现 |
| 独立切片 | 单模块/子系统可执行工程 + 参数资源 + golden | 未覆盖状态/参数均等价 |
| 完整实现 | 无 placeholder 的完整图、动态/生成一致、系统 golden | 未验证目标平台数值等价 |

`package_type: placeholder` 是有意的空实现边界，不是源码组件。`A.* / B.*` 等前缀可表达 Runtime 所有权，但不能据此批量创建项目专用 C 组件。

### 0.1 组件决策树（必须按顺序）

1. **复用已有通用组件**：搜索 `components/orpheus/builtin/`、组件 README、相邻实现与测试；只有语义、状态更新、边界条件和数值格式都匹配才可复用。
2. **组合通用组件**：优先用 composite 表达串并联、路由、矩阵、滤波、延迟、速率和桥接；不要因为外部源码有一个函数就写一个 C 组件。
3. **新增通用组件**：若算法模式可复用，先抽象通用端口、参数、状态、reset/bypass 和更新策略，再放入 builtin。必须配 README、数值 golden、实时路径审计和动态/生成一致性测试。
4. **项目独有组件**：仅限无法合理泛化的独特算法；分析记录必须写明无法通用化的原因，并固定源证据、参数布局、状态机、容差和 golden。

近似组件只能标记 `approximation`。候选映射（如“可能可用 matrix_mul”）仍保持 placeholder，直到参数顺序、状态和 golden 全部验证。

### 0.2 图与传输语义

- 画布只使用标准 `graph.connections` 音频边和 `control_connections` 控制边；禁止新增第三种 edge/handle/label。
- 跨 Runtime 混合 bus 要先按字段分类。音频字段导出为标准 audio ports；控制/状态字段只有形成 `control_source/bindable/shape/maps_to` 契约后才能画控制边。
- `488 words`、`1193 words` 等是物理事务大小，不是通道格式。音频类型写成 `[channels, frames]`，例如 `[10, 32]`，不能展平成 `[320]`。
- bus 的 offset、atomicity、sequence、epoch 和交换延迟保留在 IR；不能用无行为 BUS_EXCHANGE 节点、伪回环或“消息线”代替。
- 标准音频边只证明端口/通道/采样率/块长契约；跨 Task/跨 Runtime 数据面尚未 lowering 时仍要保留 blocker，不能宣称可运行。
- 数组控制不得编码成 string 或拆成若干标量链。可执行同 Task 数组控制必须声明数值 `type`、`kind: bulk`、`shape`、`bulk_slot`，源/目标槽容量覆盖 shape，目标启用 double bank；动态/生成路径均采用先全读、后全写、下一块提交。
- 标准 `control_connections` 禁止隐式跨 Task。跨 Task/Runtime 的数组、标量和 event 必须先建立显式控制/事务 bridge，并说明 sequence/epoch/validity 与音频字段是否同事务；event 不得降格为 float control。
- 已确认但尚不具备可执行端点或 bridge 的控制因果写入 Algorithm IR `relations`，不得为了画线提前生成 `control_connections`。

### 0.3 六门禁

每个模块独立记录：

| 门禁 | 必须回答 |
|---|---|
| `boundary` | 输入/输出、shape、通道顺序、单位和旁路是否确定？ |
| `schedule` | 属于哪个 Task/TID，何时读写，与谁交换？ |
| `algorithm` | 公式、分支、反馈、饱和、边界条件是否还原？ |
| `state` | delay/ring/ramper/sequence/reset 初值和更新顺序是否完整？ |
| `parameters` | TOP/RTC/RMDL 字段、默认值、布局、更新策略和 hash 是否确定？ |
| `golden` | 默认、非零、极值、跨块、reset、bypass 是否与参考实现对拍？ |

只有六项全部 `verified`，模块才可替换完整图中的 placeholder。

### 0.4 全量任务的停止条件

用户要求完整/全量蒸馏时：

- 从权威索引枚举全部 placeholder，按依赖顺序形成可执行工作队列；
- 每完成一个模块，立即更新实现、golden、六门禁、分析记录和 TODO，不在最后一次性补账；
- 只要仍有可由现有证据推进的 placeholder，就继续实现，不得停在“规划完成”“结构已画出”或“部分切片可运行”；
- 只有全部 placeholder 清零，或剩余项都记录了缺失证据、证伪检查和用户可执行的解阻步骤，才算本轮完成/阻塞。

## 1. 输入与输出

**输入**

- C/C++ 源码路径（文件或目录）。重点文件：
  - `process` 实现：确认信号流（级联/并联/反馈）与逐样本/逐块处理；
  - `prepare`/系数计算：确认滤波器类型与系数公式（如 RBJ cookbook）；
  - 状态结构体（`typedef struct { ... }`）：确认每实例内存、延迟线、通道数组；
  - 参数表（`OrpheusParameter[]` 或等价结构）：确认 id/默认值/范围/单位。

**输出（按复杂度选取，复杂模型全部需要）**

1. **权威证据索引**：源文件 SHA-256、稳定模块 ID、类型、Task/TID、状态、参数分区、关系、bus 字段/offset、unresolved。
2. **分析与进度**：项目内 `analysis/`、六门禁进度账本、`TODO.md`；hypothesis 必须附证伪检查。
3. **目录研究工程**：`project.yaml` + imports + component manifests + assets；结构节点用 composite，未实现叶节点用 placeholder 并阻断编译。
4. **逐模块实现**：通用组件组合或实现、参数提取脚本、hash 固定资源、独立切片与 golden。
5. **完整实现**：所有 placeholder 替换后，动态/生成双路径和目标参考完成一致性验证。

`model_tree + 单 YAML` 仅作为小型、单速率模型的兼容输入，不是复杂模型的结构权威。

## 2. 分析步骤

1. **锁定证据**：记录源文件 SHA-256；参数表逐文件固定 hash，禁止仅保存本机绝对路径。
2. **双向确认边界**：同时查看调用者和被调用者、生产者和消费者，确认 shape、通道顺序、单位、旁路和空值行为。
3. **拓扑**：按连接关系画信号流。级联（x→h1→h2→y）直接串；并联（split→多路→mix）
   用 `deinterleave`/`interleave`（多通道）或 `mixer`（求和）；反馈环（delay 回路）先记录，
   当前 Orpheus 不支持图内反馈环，需拆为任务桥/注释说明。
4. **调度与状态**：先恢复 Task/TID、周期、phase、ring/delay/ramper/sequence、reset/disable/shutdown 和跨块更新顺序。
5. **滤波器原语 → 组件映射**（见第 3 节表），严格执行 §0.1 的组件决策树。
6. **参数提取**：id 用稳定语义名、默认值、范围、单位；
   平滑参数用 `update_policy: smoothed`，其余影响行为的用 `restart_required`。
7. **签名参数**：`channels`/`sample_rate` 影响端口签名，必须 `restart_required` 且与端口声明一致；块数据保留 `[channels, frames]`。
8. **目录组件包**：按业务模块组织递归 composite manifest；端口/公开参数 `maps_to` 指向直接子实例，flatten 后必须落到原子节点。
9. **Bulk 数据**：FIR 系数数组优先使用 `$resource`（f32le/json/csv + shape + sha256）；
   运行期可写的系数槽（如 biquad 组系数）在 manifest 声明 `bulk_slots`。
10. **探针**：rms/波形/频谱等观测点 → `probe_rms`/`probe_waveform`/`probe_spectrum`
   （readback 参数自动归类为「探针」）。
11. **任务/速率域（TID）识别**：从调度代码（rtmodel.c TID switch / 分频比）提取全部 TID：
   - 每条 task 记录 `tid`、`rate_hz`、`call_interval`（= 基块率 ÷ rate_hz）、`label`；
   - `call_interval=1` 的 task 是**主音频链**（基础速率，承载 input→output）；
     `call_interval>1 && mode!=inline` 才是**降速率分析侧链**；`mode:inline` 可以产生返回主链的音频，必须按真实输出类型分类；
   - 输出到 `model_tree.task_flows`：主链用 `chains: [链id...]`，分析侧链用 `chains` 或
     `blocks: [块名...]` 显式列出（格式见 §4.1），不要把分析侧链串进主链。

## 3. 滤波器 → Orpheus 组件映射表

| 源码原语 | Orpheus 组件 | 参数映射 |
|---|---|---|
| 二阶 IIR（RBJ peaking） | `orpheus.builtin.biquad` | `type: peaking, fc, q, gain_db` |
| 二阶 IIR（lowpass/highpass/bandpass/notch/lowshelf/highshelf） | `orpheus.builtin.biquad` | 对应 `type`，fc/q/gain_db |
| N 段 biquad 串联 | `orpheus.builtin.biquad_bank` | `fcN/qN/gain_dbN`（N=0..）；系数 BULK 槽 `bqN.coefs`（5 个 float） |
| N 级 IIR（pooliir / GLXP 加速器软件近似） | `orpheus.builtin.iir_bank` | `num_stages` + `coefs`（BULK，5×N 连续） |
| 实数 FFT（半复数输出） | `orpheus.builtin.rfft` | `channels` 是并行音频通道数；FFT 点数由非零 `fft_size` 指定，否则回退图 `block_size`，且须为 2 的幂 |
| 实数 IFFT（半复数输入） | `orpheus.builtin.ifft` | `channels` 是并行音频通道数；FFT 点数由非零 `fft_size` 指定，否则回退 `block_size`，输入须为 rfft 半复数格式 |
| 加权矩阵混音（M→N 下混） | `orpheus.builtin.input_mixer_3d` | `input_channels`/`output_channels` + `weights`（BULK） |
| 响度补偿（SleepingBeauty） | `orpheus.builtin.sleeping_beauty` | `gain_index`/`offset`/`ramp_ms` + `chan_map` |
| 指数增益斜坡器（Rgainx/Rgainy） | `orpheus.builtin.gain_ramper` | `num_rampers` + `chan_map` + `ramp_ms` |
| FIR 系数数组 | `orpheus.builtin.fir` | 短数组可内联；长数组用 `$resource`（f32le + shape + sha256） |
| 增益（dB） | `orpheus.builtin.gain` | `gain_db`（平滑可 `smoothed`） |
| 静音/门限 | `orpheus.builtin.mute` | `mute` 0/1（smoothed）、`ramp_ms` |
| 开关/旁通 | `orpheus.builtin.switch` | `enable` 0/1（smoothed）、`ramp_ms` |
| 峰值限幅 | `orpheus.builtin.limiter` | `threshold_db`、`attack_ms`、`release_ms` |
| 软削波 | `orpheus.builtin.soft_clipper` | `drive_db`（tanh 归一化） |
| 饱和限幅 | `orpheus.builtin.saturation` | `limit`、`soft`（0=硬 1=软） |
| 矩阵乘法 | `orpheus.builtin.matrix_mul` | `rows`/`cols` + `matrix`（BULK，行主序） |
| 斜坡渐变混音矩阵（N表插值+IIR斜坡） | `orpheus.builtin.slc_matrix_mul` | `rows`/`cols`/`num_tables`/`tables`(BULK)/`interp_x`/`interp_index`/`ramp_coeff`/`freeze` |
| 窗函数 | `orpheus.builtin.window` | `window_size` + `coefficients`（BULK，每块从头应用） |
| 变化率限幅 | `orpheus.builtin.noise_slew` | `rise_rate`/`fall_rate`（/s） |
| 电平检测 | `orpheus.builtin.level_detect` | `mode`（峰值/RMS）、`attack_ms`、`release_ms` + `level` 探针 |
| 平方（功率/PSD） | `orpheus.builtin.square` | 无参数（逐样本 y=x²） |
| 功率谱密度（FFT bin 功率 + 指数平滑） | `orpheus.builtin.psd` | `channels`、`smoothing` + `spectrum` 探针（音频直通） |
| 多参考相干矩阵（交叉功率谱 EMA -> 相干平均谱） | `orpheus.builtin.coherence_matrix` | `channels`、`smoothing` + `coherence`/`history` 探针（音频直通） |
| 查表插值（速度/标定表，一维线性） | `orpheus.builtin.interp_lut` | `x_axis`/`y_axis`（BULK）、`x` + `y`/`history` 探针（音频直通） |
| 正弦调制（AM/颤音） | `orpheus.builtin.sine_mod` | `freq_hz`、`depth`（y=x·(1+depth·sin(2πft))） |
| 延迟 | `orpheus.builtin.delay` | `delay_ms`、`mix` |
| 多通道拆分/合并（并联各通道） | `orpheus.builtin.deinterleave` / `orpheus.builtin.interleave` | `channels` |
| 并联求和 | `orpheus.builtin.mixer` | `gain0/gain1`（dB） |
| 整数降采样/重缓冲 | `orpheus.builtin.downrate` / `orpheus.builtin.resample` | `factor`/`divisor` |
| 电平/波形观测 | `probe_rms` / `probe_waveform` | readback 探针 |
| 硬件 I/O 占位（嵌入部署，source/sink 手动填充） | `orpheus.builtin.embed_in` / `orpheus.builtin.embed_out` | `channels`/`sample_rate`；`embed_in.underruns` 探针；生成工程 `platform_io.c` 的 USER CODE 段填充 |

## 4. 小模型兼容 YAML 骨架

以下单文件 `subcomponents/model_tree` 是兼容导入格式，只适合小型单速率模型。新建复杂工程使用目录组件包和裸组件 ID，不以物理目录或 `sub:` 表达逻辑层次。

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

### 4.1 多速率 task_flows 规范（TID / 分频）

被蒸馏模型若有多个速率域（多 TID / 分频），必须用结构化 `task_flows` 表达，不能只写注释：

```yaml
model_tree:
  base_rate: 1500                # 基块率 Hz（如 48kHz / 32 样本块）
  task_flows:
    - tid: 0
      rate_hz: 1500
      call_interval: 1           # = 基块率 ÷ rate_hz；1 = 主音频链
      label: 主音频链
      chains: [input_select, preamp_part1, post_process]   # 主链：引用 model_tree.chains 的 id
    - tid: 2
      rate_hz: 375
      call_interval: 4
      mode: inline               # 内联多速率：chains 并入主链，不生成死路抽头
      label: FDP 频域处理
      chains: []                 # FDP 已在 TID0 主链（part2_fdp），TID2 仅 FFT 核心
    - tid: 3
      rate_hz: 23.4
      call_interval: 64
      label: Audiopilot FFT 噪声估计
      blocks: [Windowing, RFFT, FormCoherenceMatrixGXY]    # 或直接列块名
```

规则：

- `call_interval × rate_hz = 基块率`；主音频链只能有一条（interval=1，承载 input→output）；
- 分析侧链（interval>1 且 mode!=inline）用 `chains` 或 `blocks` 显式列出，**不要串进主链**；
- **内联多速率**（`mode: inline`，如 FDP）：chains 并入主链，不生成死路抽头；
  用于输出馈回主链的降速率组件（FDP 的 6ch 输出直接喂 Part3），区别于分析侧链（产物是控制参数，图内死路）；
- 只有不承担可观察延迟、跨 Task 交换或原子性语义的内部 `BufferRef/delayBuffer` 才可省略；
  同一 Task 内的速率变化映射为 `downrate/resample` 等速率组件；跨 Task 音频必须先使用 `async_bridge` 或允许的 `rate_sync` 合流点，若同时变速再组合速率组件，不能用 `downrate/resample` 单独替代 Task bridge；
- interval>1 的结果必须先分类：返回主链的音频走标准音频边和显式速率/Task bridge；标量控制走 `control_connections`；纯观测走 probe。禁止为了让图连通而接 `embed_out` 假装音频输出；
- `mode: inline` 只表示算法逻辑并入主链，不免除真实跨速率状态和触发顺序的建模。

## 5. 校验清单（提交前逐项过）

- [ ] 源文件/参数资源 hash 固定；Algorithm IR 无重复 key，引用闭包和 bus offset 全覆盖。
- [ ] 每个叶模块六门禁独立记录；未完成者为 placeholder，Compiler 明确阻断。
- [ ] 已按“已有通用组件 → 通用组合 → 新通用组件 → 独有组件”顺序记录选择理由。
- [ ] 画布只有标准 audio/control 边；不存在伪处理组件、无行为回环或第三种 edge。
- [ ] 数组控制使用 typed BULK endpoint/slot；同 Task 才能直接生成 `control_connections`，跨 Task/Runtime relation 有显式 bridge blocker。
- [ ] `version` + `graph` 存在；schema 校验通过（`ProjectLoader` 加载无异常）。
- [ ] `task_flows` 覆盖全部 TID：`call_interval × rate_hz = 基块率`；主链只引用音频链 id，
      分析侧链用 `chains`/`blocks` 显式列出且不串进主链（无多速率则省略 task_flows）。
- [ ] 目录 composite 的 `maps_to` 指向直接子实例并可递归 flatten 到原子节点；旧单 YAML `sub:<id>` 仅按兼容格式校验。
- [ ] 节点 id 只用 `[A-Za-z0-9_-]`（`.` 等字符会破坏代码生成标识符）。
- [ ] `channels`/`sample_rate` 与端口一致；`restart_required` 标注影响签名的参数。
- [ ] 音频 shape 为 `[channels, frames]`；words/bytes 只作为传输布局，不作为通道数。
- [ ] 长 FIR/矩阵系数使用带 shape/hash 的 `$resource`；运行期系数槽声明 `bulk_slots`（如 biquad_bank）。
- [ ] 探针参数 `readback: true`（自动归入「探针」分类）。
- [ ] `python scripts/parameter_layout.py <project.yaml>`：布局树可读、回写校验通过。
- [ ] `python -m orpheus_core.cli compile <project.yaml>` 编译通过（含设备组件则需时钟源合法）。
- [ ] 有文件输入/输出的图：输入文件放入工程目录，`file_path` 用相对路径。
- [ ] 项目内有 `analysis/`、`TODO.md`、生成/手工文件边界；代码、索引、测试和文档同提交更新。

## 6. 一键导入（兼容路径）

- UI：工具栏「⤵ 导入模型」选择 YAML → 输入工程名 → 自动打开工程（画布/参数面板立即可用）。
- API：`POST /api/projects/{name}/distill`，body `{"yaml": "<工程 YAML 文本>"}`；
  顶层未知字段（`model_tree`、`presets` 等）原样保留，编译/运行不受影响。
- **拓扑自动展开**：若 `model_tree.chains` 存在且 graph 是骨架（≤3 节点），导入时自动把每条链
  展开为子模块：流程文本（`A(param) -> B -> ...`）解析成块节点，能映射到内置组件的块用真实组件 id，
  未映射块用占位组件 id（`orpheus.builtin.placeholder`，UI 显示「组件缺失」红标），原始括号参数写入
  节点 `note` 便于查看。这里的 `orpheus.builtin.placeholder` 是旧导入器的“组件缺失 sentinel”，
  不等于目录工程中有 manifest、blocker 和编译硬阻断的 `package_type: placeholder`；新研究图必须使用后者。
- **历史多速率展开**：旧导入器可能把 `call_interval>1` 展开为 `downrate` 抽头和 `embed_out` 死路，
  这只用于浏览旧 `model_tree`，不表示控制或跨 Task 语义正确。新工作必须按 §4.1 分类为标准音频 bridge、`control_connections` 或 probe。

复杂多 Runtime/TID 模型禁止依赖此自动展开直接生成“完整图”；必须先走 Algorithm IR、六门禁和逐模块切片。

## 7. 红线与坑

- 还原的是**信号流与参数**，不是逐行翻译；重复/死代码（未连接的滤波器）应合并或标注「未使用」。
- 结构完整不等于算法完成；禁止把 placeholder、空 process、候选映射或 approximation 宣称为完整蒸馏。
- 不按外部函数“一函数一组件”。优先复用/组合通用组件，新增组件优先做通用 builtin，独有组件必须证明无法泛化。
- 生成器拥有的目录会被覆盖；手写源码、分析和 TODO 必须放在明确保留的位置，并用文档记录边界。
- 跨 Runtime/TID 的音频和控制必须走标准连接及 Compiler 契约，不得通过 UI-only 线绕过验证。
- `process` 内禁止 malloc/锁/IO/printf（实时路径）；不要往还原工程里加日志。
- 系数公式要写明来源（如 RBJ peaking：`A=10^(gain_db/40)`、`alpha=sin(w0)/(2q)`），供人工复核。
- 参数类型：float/int 按 manifest 类型下发；字符串参数（如 `type`）不能当数字解析。
- 采样率：`wav_out.sample_rate` 可留空（编译期自动跟随输入端口）。
- 输出文件一律 UTF-8 无 BOM + LF（Windows PowerShell 默认 GBK 会毁中文，用编辑器/脚本写文件）。
- 未知参数不要臆造：若 C 代码里的参数在 Orpheus 组件里没有对应项，放 `model_tree.params` 注释里说明，而不是塞进工程参数。
