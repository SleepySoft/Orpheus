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

`orpheus_core/tests/test_data_ports.py`使用非占位C组件验证129×2数组和标量的同轮计算、
每三块更新的慢结果保持、初值零、同tick更新可见、扇出、悬空输出、复合组件保存/展开、
分数采样率时间线与非法连线拒绝；动态DLL和生成工程各自的全局/Task入口共四种入口，
运行后逐项比较非零结果与帧索引。
这验证框架数据链路，不代表外部模型的占位算法已经实现。
