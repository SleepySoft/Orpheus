# 时钟链与静态调度（clock chain / static schedule）设计记录

> 状态：v1 已落地（见文末「落地方案 v1」）。
> 对象：宿主驱动粒度、跨速率合流、buffer/delay、sink 消费的时钟链。

## 背景与触发

`rate_sync`（多速率异步合流）落地后，暴露出宿主驱动层面的时钟链问题：

- 一份含跨速率合流的图（如 `example-a` 的 roof/spkr 到发散检测，或 `rate_sync` 合流）里，不同分支的块长不同（24 / 32 / 96）。
- Per-node 调度量已经由编译器从上游推导（`node_configs[].{block_size, frames}`），并非全局一块。
- 但宿主（`main.cpp` / `rt_host.cpp`）仍用单一全局 `plan.block_size` 推进 `process_block()`，runtime 内部又靠 `if (divisor>1 && counter%divisor)` 让每个节点按自己的 divisor 推论触发。
- 结果：像 `wav_out` 这类薄 sink 在每个宿主 tick 都被调用，却按它输入缓冲的固定容量（如 96）整块落盘，导致同一 96 帧块被重复写（表现为 96000 帧/2s，而 `plan.duration_frames` 正确为 24000）。

这本质是每个分支自循环、各按各的节奏跑，缺少一张统一的定时表。

## 商业系统怎么处理时钟链

汽车音频 DSP、播控、Simulink 自动生成码（本 repo 蒸馏源 `Model_Target` 即属此类）遵循同一组原则：

1. 单一主时钟 + 整数分频派生：所有 rate（1.5k / 2k / 250 / 31.25 Hz 等）都来自同一个主时钟（48 kHz 帧同步 / 晶振）的整数 decimation。系统时钟必然相关，任意两条链的块长必然有公倍数，因而必须存在整齐的同步点。
2. 编译期静态调度表，而非节点自循环：最细主步长（各分支块长的 LCM/对齐步长）+ 一张确定、无竞争的触发表。每个 tick 列明哪个节点此刻跑、跑多少样本、读哪个 buffer、写哪个 buffer。运行时是单线程顺序执行的统一骨架，节点从不自己决定何时运行。
3. 同步点 = 显式 rate-bridge / 帧同步 FIFO：跨 rate 分支合并必须落在明确同步点。调度器保证在某主 tick 两侧分支都到达整数帧边界，只在那一个时刻做一次对齐交换。FIFO 深度由确定性延迟预算决定，是约束而非随意参数。
4. sink 也是调度表上的端点：DA/文件端落在某个 rate 域，在固定 time-slot 消费固定样本数，消费节奏由表下达，而非自行推断，因此不会因帧数不同而重复读。

## 对照当前 repo

- 每节点块长/帧数：编译器已从上游推导（per-node，非全局）。应然：保持。
- 宿主驱动步长：单一全局 `plan.block_size`。应然：取调度主步长（各节点 frames 的 LCM/对齐步长）。
- 跨 rate 分支合并：无显式同步点；rate_sync 靠 LCM buffer。应然：调度表显式排定 rate-bridge 同步点。
- sink 消费：每个宿主 tick 都被调用、按输入缓冲容量整块写。应然：由表在有数据的 time-slot 消费。
- 不匹配行为：静默重复写（96000 帧）等。应然：暴露为编译/运行期报错，不静默掩盖。

## 落地方向（结论）

- 由 compiler 在 plan 里额外推导并输出一张静态调度表：最细主步长 + 每节点触发相位/每次样本数 + 跨 rate 分支的同步点（rate-bridge 位置与 FIFO 深度，含延迟）。
- Host（`main.cpp` / `rt_host.cpp`）不猜全局值，按这张表推进一个主 tick。
- `plan.block_size` 降级为单速率/回退默认（单速率图下推导值等于现值，测试应无损）。
- 把时钟链不匹配当错误显式暴露（compile 或 run 报错），不打表面补丁掩盖（呼应决策：不回退、有错直接报、避免噪音）。
- 跨 rate 合流必须声明经过 rate-bridge 同步点，否则编译报错。

## 落地方案 v1（已实现）

**调度表模型**：每个节点的触发间隔统一折算成图采样率帧数
`I_n = frames_n × plan.sample_rate / node_rate_n`（`frames_n` 为节点量子，
`node_rate_n` 为节点有效采样率；整除不了即编译报错「时钟链不匹配」，不静默掩盖）。
主步长 `tick = GCD(所有 I_n)`，节点周期 `period_n = I_n / tick`。
runtime/generator 的触发谓词统一为 `(block_counter + 1) % period_n == 0`。

- 单速率图（含 downrate 超块链、resample 异速链）：推导出的 tick == plan.block_size、
  period == 旧 divisor，行为逐字节不变（大量一致性测试保底）。
- 跨块长合流图（24/32/96）：tick=8、周期 3/4/12/12，sink 只在有新鲜数据的
  time-slot 触发 —— 96000 帧重复写（实际 4×）消除。

**rate-bridge FIFO**：指向 merge 节点（`scheduling.merge`，如 rate_sync）的边在
plan.buffers 里标记 `rate_bridge: true`，深度 = merge 节点量子（输入块长 LCM，
天然是各源块长的公倍数）。runtime/生成代码为该边旁路一个 staging buffer：
生产者照旧写 staging（自己的块长），触发后由骨架按写游标把小段 memcpy 进桥接
buffer；merge 节点在同步点整块读取。约束：桥接源必须是「整写者」（每次触发完整
交付其输出端口块 = 节点量子），downrate/resample 等部分产出组件直连接入会在
编译期报错，提示中间加一级直通组件。

**plan 新增字段**（旧 plan 无此字段时全部回退旧行为）：
- `plan.schedule = {"tick": int, "periods": {node_id: int}}`
- `node_configs[*].period`（生成路径直接消费；0/缺省 = 回退 divisor）
- `buffers[*].rate_bridge: bool`

**宿主**：`main.cpp` / `rt_host.cpp` / 生成的 file-clock main / `host_win.c` 的推进
步长统一改为 `schedule.tick`（缺省回退 `plan.block_size`），不再自猜全局块长。

**已知边界**：相位全部对齐在 tick 0（所有节点首次触发于各自 period-1 tick），
不支持任意相位偏移；merge 输入要求同速率（连接校验已有的 sample_rate 相等保证）。

**同 Task 速率继承**：未显式声明采样率的音频端口跟随唯一上游速率，显式速率仍做硬校验；
`resample` 的输出以 `in:sample_rate/param:factor` 从本地输入推导，支持级联。
因此 `resample(16) → downrate(4)` 表示先降至原采样率的1/16，再按同采样率积累4个块，
不是再创建一个独立时钟。降采样后的普通组件及 `wav_out` 继续跟随该速率；
不同输入速率不会自动合并或隐式重采样，真正跨 Task 的连接也不采用此继承规则。

## 范围提示

这是 runtime/host + compiler 的架构级改动（引入静态调度表），非 rate_sync 组件层面的小补丁。落地前需保证单速率图行为不变（现有大量 host/runtime 测试保底）。

## 通用有理SRC、拆块与重叠窗口

整数降采样器 `resample` 保持默认 `mode=0`（每组滑动平均）的既有行为。
`mode=1` 为纯选点：每组 `factor` 个输入中复制0基 `phase` 位置的样本，
`0 <= phase < factor`，默认相位0。组内位置跨调用保留；reset清空组进度、
输出写游标和历史，但保留配置。输出仍在完整抽取组结束时提交，调度和速率声明不变。
选点不附加滤波，也不做乘法，保留被选样本的负零及NaN；
上游应按应用需要先完成抗混叠滤波，不能把选点和平均当成等价算法。
`mode/phase` 均须重启后生效，非法配置在prepare明确返回错误。

三种关系分别表达采样率变化、批次转换和历史窗口，不能互相替代：

| 组件 | 输入→输出示例 | 调度与约束 |
|---|---|---|
| `rational_resample` | 416帧@48kHz → 96帧@144000/13 Hz | `up=3, down=13`；输入长度乘up须能被down整除。不足先用downrate组块，不截断样本数。 |
| `block_split` | 128帧 → 4次32帧，同采样率 | `factor=4`；prepare的block_size为128，process的frame_count为32；下游回到小块节拍。 |
| `circular_buffer` | 每128个新样本 → 256点窗 | `hop_size=128, frame_size=256, num_frames=1`；输出容量256，但下游每128点帧移触发一次。 |

### 有理采样率和ABI

编译期使用精确分数传播采样率、计算周期；plan对非整数速率附加
`sample_rate_ratio: [numerator, denominator]`，供追溯与验证。执行所用Hz值为double，
不先取整为11076或11077 Hz。最终周期仍必须落在Task采样率的整数帧边界；
当前不支持任意分数帧触发，应选择完整比例批次。

ABI v5将`OrpheusConfig.sample_rate`和`OrpheusProcessContext.sample_rate`改为double；
动态Runtime、生成工程和所有组件须一起重建。旧二进制由ABI版本检查拒绝，
不能混用。Task/设备时钟仍为整数；WAV头也只允许整数Hz，分数速率必须先转换后写WAV。

`rational_resample`在prepare中生成Blackman窗低通多相FIR系数，截止频率为
`0.45 * min(1, up/down)`（输入采样率归一化），每相作DC归一化；process只访问预分配状态。
`taps`控制每相输入历史长度，默认128。非1:1转换的群延迟为`(taps-1)/2`个输入样本；
1:1直接透传。默认滤波器是通用实现，不宣称匹配任何外部模型系数，也不保证所有比例在默认抽头数下都有同样阻带指标。

### 拆块的启动与快照

同源静态调度以周期末交付完整大块。拆分N份时，前N-1个小块周期输出零；
完整输入就绪后先快照，再顺序交付N个小块，避免上游下一批局部写入破坏待消费数据。
因此128→32的启动延迟是96样本；这属于目标批次适配，不是模拟核间通信延迟。
只支持此同源相位契约，不用于任意相位、独立线程或独立时钟。

### 窗口的块长不等于帧移

窗口输出块长为`frame_size*num_frames`，输入必须是`hop_size*num_frames`。
历史初始化/reset为零。容量不足、批次不匹配直接报错，不再裁剪成空输出。
交付周期从输入沿图传播；只有downrate/resample等分频组件乘输出周期，
拆块器缩短周期。plan在需要时记录`frame_step`，Runtime和生成的全局/Task入口
以该步长推进本地绝对帧索引。例如256点窗、128点帧移，下游每次处理256点，
但frame_index每次只前进128点。

重叠窗口不是连续PCM流，不得通过加gain绕过限制直接接入普通rate_sync合流；
需要先显式恢复连续样本语义。当前不自动补偿FIR群延迟或其它路径延迟，调用方应按功能保留这些延迟。

## 非音频计算数据（2026-11）

`control` / `bulk` / `debug` **端口**承载算法中间结果，不是参数控制连接，也不是
可寻址的 BULK 参数槽。例如129点复数谱、功率向量、互谱矩阵和单个判定标志，
可以在同一次调用中共同作为输入，不需要填充为等长PCM。
与参数接口混合使用时，必须声明覆盖/备用/合成规则，不能让连线暗中改变参数含义；
见[组件模型：混合交互](../03-component-model.md#混合交互的使用边界)。

### 数据尺寸与时间线分离

- 非音频端口当前支持 `sample_format: f32`；其它格式在编译期显式拒绝，不能静默
  以float解释整数内存。端口 `channels × block_size` 表达元素数量，缺省为1×1；
  复数/矩阵的具体轴和布局由组件接口契约声明。
- 连线两端类型、格式、channels和block_size必须匹配，不做隐式广播、填充或转换。
  非音频端口的sample_rate不作为数组连线匹配条件，也不定义独立采样率，统一随驱动
  时间线传播；数组长度不参与时钟周期计算。
- 音频输入决定节点处理量子；只有同步数据输入时，沿首个同步源继承处理量子及
  精确时间线。没有同步输入时使用所属Task节拍。全部同步输入的交付周期必须一致。
- `config.block_size` / `ctx.frame_count` 仍是处理时间量子；数据的元素数必须从
  对应 `OrpheusBuffer.frame_count × channels` 读取，不能用ctx.frame_count遍历数组。
  `frame_index`继续按时间线步长推进，不按频点/矩阵元素数推进。

### 同步输入与读取最新值

输入端口新增 `timing`，缺省为 `sync`：

```yaml
ports:
  - {id: spectrum, direction: input, type: bulk, channels: 2, block_size: 129}
  - {id: decay, direction: input, type: control, channels: 1, block_size: 1, timing: latest}
```

- `sync`：参与节点交付周期的推导与校验；上游到期后，本轮下游可直接消费新结果。
- `latest`：仅用于同Task非音频输入，不驱动或减慢节点执行；复用生产者最近一次
  写入的数据，首次产出前为零。同一tick生产者到期时，拓扑序保证先生产、后消费，
  因而当轮即可看见更新，没有参数控制链的一块延迟。
- latest边仍参与拓扑排序与环检测；不支持借latest构造隐式反馈。循环状态需要显式建模。
  它表示最近一次写入，不为组件的“部分写入/未发布”行为提供额外事务保证；
  需要快照时，组件必须显式发布完整数据。
- 这些边使用现有按端口绑定、零初始化且持续保留的F32缓冲；动态Runtime与生成的
  全局/Task入口共享同一plan调度。plan额外记录buffer的`type/timing`和节点的
  `input_port_timings`，供接口追溯。

当前只支持同Task数据计算；跨Task/线程/Runtime必须有显式数据快照或事务桥，
直接连线会编译失败。音频rate_sync/async_bridge及其PCM FIFO不能冒充数据事务桥。
复合组件边界映射到实际叶子端口，执行时序以叶子声明为准；公开接口应保持一致。

### 验收证据

`complementary_coefficients`是纯计算数据组件：逐行把N通道系数`x`展开为
`[x, total-x]`的2N通道BULK，默认total=1。前半直接保留位型，后半只执行一次
float32减法；不夹到0～1，不叠加平滑，不用乘加改写减法。适用于互补混合权重、
剩余系数和交叉渐变；total须有限，输入特殊IEEE值遵循原生减法传播。
元素数独立于时间量子，无音频输入的整条计算图可直接按Task节拍执行，
不需要伪造PCM或借参数投递。原生冒烟覆盖特殊位型与错误输入不部分写入；
`orpheus_core/tests/test_complementary_coefficients.py`验证129/387元素按128/384
帧节拍同轮计算、非单位total及动态/生成全局与Task四入口逐位一致。

`complex_gain_split`接受Re/Im交织BULK及同通道的实增益BULK，
先计算`reverb=gain*x`并保留float32舍入，再计算`impulse=x-reverb`。
这不是`(1-gain)*x`，也不能融合乘减；组件显式保留乘积舍入。
同一增益分别作用于实/虚部；不夹增益、不增加平滑或历史。
`elements`是所有批次合计的复数点数，复数payload为`elements*2`行，
增益为`elements`行，上游交付周期不变。两输出可各自悬空，
全部输出先校验再写，输出不得与输入/另一输出重叠。
`tests/complex_gain_split_smoke.c`验证IEEE特殊值、舍入顺序和错误拒绝；
`orpheus_core/tests/test_complex_gain_split.py`以129/387点和多通道数据验证四入口逐位一致。

`complex_power`逐点计算`re*re+im*im`，分别保留两次乘法舍入，
每批每通道从负零开始按频点顺序累加；能量为`log10f(sum+epsilon)*10.0f`。
它不求均值、不除FFT长度、不加单边谱倍率，也不替代带历史的PSD平滑或冲击检测。
`power`尺寸为`bins*batches × channels`，`energy`为`batches × channels`，
三批次必须保留三份独立能量，而不是混成一份。NaN/Inf直接传播，epsilon须有限且非负。
原生测试覆盖特殊位型、零偏置、顺序归约和输出校验；
`orpheus_core/tests/test_complex_power.py`核对功率及能量数值和四入口逐字节一致，
通过独立尺寸的张量采集器捕获每份结果，不以数组长度冒充时间量子。

`dual_trapezoid_smooth`提供同一实向量的两组独立梯形递推，
严格计算`(x+x_prev)*((1-alpha)*0.5)+y_prev*alpha`，不是普通EMA或改写的IIR算式。
每点每通道保存历史，批次按时间顺序推进同一组状态并跨process保留，
这与`complex_power`的各批独立归约不同。初始历史默认正零保持旧行为；
可用有限`initial`指定输入及两份输出历史的共同初值，首个有效调用时惰性读取，
支持在该调用前更新。初始化后修改initial/alpha均保留当前历史。
reset恢复未初始化冷启动而保留配置，下一有效调用使用最新initial；
悬空输出仍更新其状态，零处理量不推进，无效输出不部分更新状态。
历史只在prepare分配，输入/输出及两输出间禁止重叠。
`orpheus_core/tests/test_dual_trapezoid_smooth.py`验证单/三批与多通道四入口逐位一致；
原生测试另覆盖跨块、动态系数、边界系数、特殊位型、惰性非零初值、
失败prepare不清历史、reset、NULL输出推进与输出校验；四入口同时保留旧默认零初值用例。
现有音频PSD会重新FFT并使用不同递推，普通IIR的重排也改变浮点舍入，因此不借它们冒充该数据算子。

`pool_delay`是音频共享池延迟，不是各通道独立延迟线：
每块先按各通道延迟前缀和读旧池，再统一写入；变长重解释旧池，不搬移历史。
`enabled`默认true兼容原调用，false时逐字节输出干声且冻结整个池/游标；
重新启用继续旧历史，而不是末端选择器后仍在运行的延迟。
延迟数组仍可在停用期间更新，reset清池/游标但保留延迟与启停配置。
干声和延迟路径共同校验格式、通道、容量与输入输出重叠，无效调用不推进状态。
原生测试核对池/游标冻结、继续、reset和错误拒绝；
`orpheus_core/tests/test_pool_delay.py`保留默认启用用例，并验证22路/512槽持续旁路及
跨块启停、停用期间变长后的四入口参考结果与逐字节一致性。

`bulk_ring_delay`用于计算数据的固定容量环形历史：每批先完整写入bins行，
再从本批起点减去delay_elements的位置按容量取模读取bins行，最后推进游标。
各通道独立，批次按时间顺序推进；偏移不是图时间长度，变更偏移保留旧池。
写完整批后读取的覆盖行为与逐样本音频延迟不同，容量也不是“最大延迟加一”的隐式推导值。
支持非整帧偏移、多圈偏移、NULL输出推进、reset、特殊位型复制及无效输出不改状态。
四入口驱动默认仍采3块，向量历史测试显式延长到至少绕池两圈，
并断言读到非零历史，防止长延迟只验证了全零冷启动。
迁移SHARC `circindex`时，原builtin只纠正一圈，要求增量绝对值不超过容量
（CCES 2.11.1 SHARC编译器手册“Circular Buffer Increment of an Index”）。
通用算子的多圈取模是额外能力，不代表可忽略原调用的合法定义域；
来源验收须区分“原调用加契约shim”与厂商机器码或硬件仿真。

`bulk_deinterleave`将`elements × channels`的f32计算数据拆为单通道`out0..outN-1`，
`bulk_interleave`将单通道`in0..inN-1`按索引组合成交错数据；尺寸独立于处理量子。
通道数支持1..128，覆盖80项状态总线；80通道及128通道上界均有原生和四入口验收，
129通道明确拒绝且不修改既有配置。容量扩展不改变原通道顺序、位型或调用节拍。
它们不是音频端口，不按ctx.frame_count截断129/258/774等数据，也不改变时钟。
逐元素复制原位型，包含负零、NaN payload和复数布局中的实/虚槽；不解释或转换复数。
并通道的NULL输入明确补正零，拆通道的NULL输出忽略；无效尺寸/格式/重叠在写输出前拒绝。
只支持f32，没有把i32端口偷偷当成float使用；原音频交错器行为不变。
原生测试和四入口通道反序/重复/补零验收见`tests/bulk_channels_smoke.c`、
`orpheus_core/tests/test_bulk_channels.py`。

`bulk_slice`按线性f32槽索引复制`out[i]=in[offset+i*stride]`，可提取固定区间或矩阵列：
例如100通道×130行输入的`offset=99,stride=200`提取末通道的65个偶数行槽。
输入/输出通道数各1..128、每通道元素数各1..65536；输出的线性槽按输出通道数重新交错解释。
末槽必须在输入范围内，配置校验使用64位索引运算，失败保留旧配置，不补零或截断。
该桥不认识复数、频率或矩阵含义，不作算术或位型转换，不适用于i32端口；
输入必需，NULL输出仍检查输入，重叠或无效缓冲在写输出前拒绝，零处理量不写。
数据行数独立于调用量子，没有新增时间延迟；原生与四入口测试见
`tests/bulk_slice_smoke.c`、`orpheus_core/tests/test_bulk_slice.py`，含真实13000槽矩阵。

`bulk_sum`对f32计算数据的每个通道独立有序归约：从第一项初始化，之后逐项加法并舍入为f32。
输出每通道一项，不广播、不重新FFT、不开方或跨调用累计；单项输入保留原位型。
支持1..128通道、1..65536元素，输入行数不影响调用节拍。
这与音频`spectral_reduce`的初始零、音频帧截断及整块广播契约不同，后者行为不变。
输入必需，NULL输出仍检查输入，缓冲无效或重叠在写输出前拒绝。
原生测试覆盖能区分加法重排的抵消序列、负零、单项NaN位型及非法配置的原子拒绝；
四入口覆盖65/387项归约和128通道单项边界，见`tests/bulk_sum_smoke.c`、
`orpheus_core/tests/test_bulk_sum.py`。

`frame_energy`把交错f32音频固定窗口归约为一个同步f32 BULK标量：
先逐帧按通道声明顺序累加平方，再按帧顺序累加（总和初值负零）。
平方及各次加法分别舍入，不能改为逐通道归约后相加；不平均、转dB、广播或跨调用累计。
channels支持1..128，frames支持1..65536，窗口形状独立于调用节拍，不改变时钟。
输出可直接接同Task数据计算，不借用下一块的参数控制链；NULL输出仍检查输入。
零处理量不写，配置/缓冲/重叠错误在写输出前拒绝；支持外部分配state block。
与既有电平检测和音频谱归约职责不同，不改变后两者行为。
原生边界与单/双/64通道四入口测试见`tests/frame_energy_smoke.c`、
`orpheus_core/tests/test_frame_energy.py`，原生另覆盖128通道。

`bulk_db`是同shape实数数据的显式功率/幅值dB桥，不借用音频端口或参数控制链。
mode=0按`log10f(x+epsilon)*factor`换算，mode=1按`expf((x*2.30258512F)/factor)`还原；
quantity=0为功率（factor=10），1为幅值（20）。epsilon只用于正向换算，必须有限非负。
加法、乘法、除法分别舍入，不改写为powf，不取绝对值或钳位NaN/Inf；
零偏置下零输入产生负无穷，负输入保留对数的NaN语义。
1..128通道、1..65536元素，模式/物理量/偏置可块边界更新，无历史、不广播、不改变节拍。
输入必需，NULL输出仍校验，非法配置/缓冲/重叠在写输出前拒绝。
原生边界及正反向单项、多通道向量、128通道四入口见`tests/bulk_db_smoke.c`、
`orpheus_core/tests/test_bulk_db.py`。

`bulk_scale`将f32向量乘一个显式标量。gain为f32，integer_gain为i32，
通过input_alternatives约束必须且只能连接一个；整数先按C显式转换为f32再乘。
这不是非零布尔选择器：整数2会加倍，零因子仍执行乘法，保留NaN*0与有符号零。
两个标量输入均声明latest，分频生产者不触发时保留最近数据，不改变向量消费节拍；
同次触发仍依数据拓扑顺序读取，不借用块末参数控制链引入额外延迟。
支持1..128通道、1..65536元素，输入必需，悬空输出仍检查向量及所选标量；
错误或重叠在写输出前拒绝，不在实时路径分配内存。
原生覆盖类型/互斥/零乘法/整数精度转换及外部state block，四入口覆盖两种因子、
多通道向量与128通道，见`tests/bulk_scale_smoke.c`、`orpheus_core/tests/test_bulk_scale.py`。

`bulk_one_pole`提供同shape逐元素一阶指数平滑：
`y=(1-alpha)*x+alpha*y_prev`，两次乘法各自舍入；可选择先对输入取绝对值。
每项历史独立、跨调用保留；可选i32标量`update`为零时保持全部历史，未连接则每次更新。
这不是音频逐样本滤波：数据各行是独立维度，不在一次调用中互相递推，也不改变时钟。
修改alpha/绝对值开关不清历史，reset恢复正零；NULL输出仍推进，所有尺寸/格式/重叠先校验。
仅prepare分配历史，失败保留旧配置与历史；支持1..128通道、1..65536元素。
原生测试覆盖门控/保持、动态参数、reset、NULL输出、错误原子性与边界系数；
四入口覆盖标量三通道及129/387项向量，见`tests/bulk_one_pole_smoke.c`、
`orpheus_core/tests/test_bulk_one_pole.py`。

`trapezoid_ratio`将两份独立f32向量分别按
`y=(x+x_prev)*((1-alpha)*0.5)+y_prev*alpha`平滑，再计算
`numerator_y/(denominator_y+epsilon)`。每点/通道保存四组历史，顺序批次共享各自历史，
与双速PSD算子“同一输入、不同系数”的语义不同；不作额外限幅、开方或能量归约。
参数修改保留历史，reset恢复正零历史；悬空输出仍推进两路历史。
`alpha`须为有限[0,1]，`epsilon`须有限非负；epsilon=0时保留正常IEEE除零语义。
分母加数在平滑之后加入；`division_mode=0`（默认）保留直接除法，
`division_mode=1`则先将`1/(denominator_y+epsilon)`舍入为f32，再乘`numerator_y`。
两种顺序不可互相重排；切换顺序不清历史，reset保留配置。原生及四入口测试见
`tests/trapezoid_ratio_smoke.c`和`orpheus_core/tests/test_trapezoid_ratio.py`。

`bulk_subtract`对同shape的两份f32 BULK逐点执行`a-b`，支持独立通道与行数，
不以数据行数改变宿主时间量子。两个输入均必需，允许输入共享存储；
悬空输出可跳过计算，但仍校验输入。输出禁止与任一输入重叠，
全部校验通过才写，不截断NaN/Inf、负零或次正规数。
它使`(a-b)*z`中的系数减法先独立舍入，不能偷换成`a*z-b*z`。
原生边界与四入口测试见`tests/bulk_subtract_smoke.c`、
`orpheus_core/tests/test_bulk_subtract.py`。

`delta_hold`消费f32逐批向量，按严格增量阈值触发，再按批次计数保持，
输出真正的i32标志，不通过浮点边或参数调用伪装整数数据。
触发批次清计数，后续仅在上一状态为1且counter<hold_ticks时保持并递增，
保留前输入/计数/状态；批次连续推进，NULL输出也推进，零处理量不推进。
阈值和保持批数可块边界修改而不清历史，reset恢复零历史。
混合类型采集fixture将f32输入与i32结果分别注册为对应类型BULK槽，
驱动按字节读取，不数值转换；四入口测试必须同时观察到触发和保持到期。
非音频端口现支持f32与i32，连接两端格式必须严格相等，不进行隐式转换。
plan保留缓冲格式及每个输出端口格式；动态Runtime分别使用float/int32_t存储，
生成图声明对应C数组和ABI格式，包括悬空输出的丢弃缓冲。旧plan缺少输出格式时仍按f32处理。
此扩展不把非音频边变成跨线程同步桥：跨Task数据边仍拒绝，
latest也不提供跨线程同步；音频速率/任务桥仍是f32路径。

`orpheus_core/tests/test_data_ports.py`使用非占位C组件验证129×2数组和标量的同轮计算、
每三块更新的慢结果保持、初值零、同tick更新可见、扇出、悬空输出、复合组件保存/展开、
分数采样率时间线与非法连线拒绝；动态DLL和生成工程各自的全局/Task入口共四种入口，
运行后逐项比较非零结果与帧索引。
这验证框架数据链路，不代表外部模型的占位算法已经实现。

### 数据方向保持与阈值限速

数据域的`bulk_guarded_slew`不是逐样本的`noise_slew`：每次交付对同shape各元素
推进一次，独立保存前输出、前delta和前change。正/负变化分别由`attack>=0`、
`decay<=0`夹限；前后delta同号时保留前change，否则从零候选开始。
在`threshold>=0`范围内，以候选slew抑制方向反转越冲；可选f32单项`bound`
非零时，用`slow>=0`替代零候选，使受边界约束的值仍可缓慢移动。
速率单位是“每次交付”，不除音频采样率，不把数据行解释为时间。
`initial`惰性用于首次有效调用，reset恢复未初始化态；动态参数不清历史，
NULL输出仍推进，零处理量/错误不推进，输入/标记与输出禁止重叠。
通道1..128、每通道1..65536元素，prepare分配三份历史，process不分配。

### 首批频域闭环：FFT / IFFT / Overlap-Add

布局不相同时使用显式桥接，不重复FFT：`halfcomplex_decode`接受旧音频类型的`in`
或带`layout: halfcomplex`、`frame_size: fft_size`的数据`spectrum`，两者必须且只能接一个。
它把每批N槽的`hc[k]=Re(k)`、`hc[N-k]=Im(k)`转换为无布局标签的兼容BULK，
选择从`first_bin`开始的`bins`个非负频点；精确索引为
`((batch*bins+bin)*2+part)*channels+channel`，part=0/1分别为实/虚部。
DC/Nyquist虚部写正零，其余位型逐字节保留，无共轭、缩放、窗函数或量化。
`fft_size`允许2～65536的偶数，不要求二次幂；所有批次共享一次交付。
可选`tick`输出为i32单项，每次非零量有效调用写1，用于同Task成功处理依赖，
不是计数器、时钟或跨Task同步；零量/错误不写，全部输出校验后才写入，输出之间也禁止重叠。
例如256槽变258槽、三批768槽变774槽，仍沿上游128/384帧移交付，
不是新增音频时长。输出只兼容明确采用该交织规则的无标签算法接口，
不能假定任意无标签BULK都具有相同布局。该桥不替代频域算法核。
`tests/halfcomplex_smoke.c`验证特殊IEEE位型、选带、边界及非法输入拒绝；
`orpheus_core/tests/test_halfcomplex_decode.py`逐位核对动态/生成的全局与Task四入口，
覆盖双通道、十通道、单/三批次、两种输入及选带，并断言原交付周期不变。

逆向使用`halfcomplex_encode`：完整Re/Im交织BULK转换为旧音频类型`out`
或带halfcomplex/frame_size标签的`spectrum`，可同时输出。`bins`单独用于静态
端口尺寸，prepare严格要求`bins=fft_size/2+1`，不缺频点、不隐式补齐。
DC/Nyquist只保存实部，其输入虚部不参与实频谱打包；一般复数FFT需要保留这些
分量时不能使用本桥。其余半复数槽位原样复制，不做IFFT或归一化。
所有输出校验后才写入，禁止输入/输出及两输出间内存重叠。
`tests/halfcomplex_encode_smoke.c`覆盖特殊位型、N=2/6/16、双输出及错误拒绝；
`orpheus_core/tests/test_halfcomplex_encode.py`以6/8通道、单/三批次逐位对比
原HC与decode→encode后的旧/具名输出，动态/生成全局与Task四入口一致。

- 扩展`rfft`：保留旧音频`out`和halfcomplex/magnitude/power模式；新增
  `spectrum`数据输出，始终为未归一化halfcomplex。非法批次不再静默退化为单帧。
- 扩展`ifft`：`fft_size=0`保留自动单帧；显式设置4～1024的二次幂即可逐帧处理
  完整批次，例如768槽明确表示3×256而非768点FFT。新增`spectrum`输入与旧`in`
  互斥，输出含1/N归一化。遍历依据buffer实际长度，不使用ctx.frame_count。
- 新增`overlap_add`：接收`frame_size*num_frames`的时域展开帧，按`hop_size`
  顺序累加，输出`hop_size*num_frames`的连续样本，跨批次保留尾部；reset清零。
  帧移须不大于帧长。不使用`scheduling.split`，不改变上游交付周期，也不隐藏
  分析/合成窗及归一化。`window`的repeat系数乘法可显式放在分析端或合成端。
- 频谱数据端口声明`layout: halfcomplex`与`frame_size`，编译器同时校验payload
  长度、布局和分帧；输入尺寸继承不再依赖处理量子。声明规则见
  [组件模型](../03-component-model.md#数据布局分帧与互斥输入)。

[可运行示例](../../../examples/spectral_roundtrip.yaml)使用矩形分析窗（不乘窗口）：
`circular_buffer → rfft.spectrum → ifft.spectrum → overlap_add → gain(0.5)`。
256点、128帧移时重叠倍率为2，0.5由显式gain实现。组合的启动延迟为128帧，
不是每批384帧；欲取完整尾部须继续输入零，当前EOS不会自动刷新所有算法状态。
OLA本身只将输入帧原点的首hop样本输出，没有另加一帧延迟。

验收：

- `tests/spectral_pipeline_smoke.c`以独立DFT核对DC、Nyquist、虚部符号和各批次/通道
  的打包位置；验证3×256逆变换、与处理量子不同长的实际buffer、非法批次/容量拒绝、
  OLA跨调用尾部及reset。prepare预计算/分配，process无分配及IO。
- `orpheus_core/tests/test_spectral_pipeline.py`验证1帧6通道、3帧8通道、旧音频接口和
  旧自动点数配置；动态/生成的全局与Task四入口WAV逐字节一致。重建误差不超过
  2个int16量化单位，覆盖非零独立通道、128帧启动延迟和补零尾部；时间线步长
  分别为128/384，不按频点数量推进。
  测试私有的spectral_gate以128/384音频帧为处理量子，传递256/768槽频谱并检查
  绝对帧索引；因此也实测了IFFT输入payload与config/ctx量子不等长的完整执行链，
  不仅是编译shape断言或一次孤立的组件调用。

对研究FDP边界的核对：全速叶子为6通道256→256→128；低速叶子为8通道
768→768→384，同一实例顺序处理3个hop。上述标准能力覆盖这些**形状和节拍**需求。
本批未替换研究库中的占位叶子，也未改变其TODO/完成标记；原129个复数bin与halfcomplex
仍须显式编解码，合成窗与平台FFT归一化仍须按源码索引对拍。不能由通用闭环通过
推导整个FDP或外部模型已经数值等价，亦不解除其他算法、状态和参考对拍门禁。
