---
title: 组件模型与 ABI
type: HOW
order: 3
up: '[[00-index]]'
tags: [orpheus/how]
---

# 组件模型与 ABI

组件由 manifest 和 C/C++ 实现组成。

## manifest 能力

- 声明 id、名称、类别和源码。
- 声明端口、参数、内存、平台和执行属性。
- 参数可影响签名，也可 bindable 作为控制目标。
- 端口可以引用参数实现可变通道数。

`n_way_mux` 每路支持 1..64 通道，输入路数仍为 1..32。`ramp_ms=0`
时整组立即切换：选中的输入逐字节拷贝，未接输入输出正零，不读取或混入未选分支。
因此两路选择器可用一路未接、一路接音频来实现统一硬门；它不负责停止上游计算或冻结上游历史。
默认通道数和既有淡化行为不变，原生 prepare 拒绝超过 64 通道。
`select`（1 起）与 `select_index`（0 起）的原生槽同步回读，经名称和 ID 访问均调用组件选路逻辑，
动态与生成路径一致。

`fir_bank` 用于共享或每通道独立的 FIR 系数与状态，不改变旧 `fir` 的接口。
支持 1..64 通道、每通道系数跨度 `num_taps` 为 1..4096、每通道有效抽头数
`taps_per_channel` 为 0..num_taps（空为统一长度）。0 抽头输出正零且不推进该通道历史，
未用系数不参与运算。系数按最新到最旧样本排列，float32 乘加按最旧到最新顺序执行，
与翻转系数、正向历史窗口的通用 FIR 参考顺序对齐；不以代数等价忽略累计舍入差异。
初始 CSV 为空时显式采用单位脉冲，
非空必须严格提供完整系数；每通道模式按固定跨度排列，不接受截断或非有限初值。
运行期 `coefs` / `coefs_per_channel` 浮点 BULK 在块边界双 bank 更新，shape 分别为
`[num_taps]` / `[channels,num_taps]`。每通道模式的总系数数不得超过 32768，
固定 active 存储上限 128 KiB；历史只在 prepare 按实际 `channels*num_taps` 分配，
process 不分配内存。有效抽头数/跨度变化需要重启，不重解释旧历史。
`coefs_mode=banked` 时，`num_filters` 个系数集按 `[num_filters,num_taps]`
存入 `coefs_banked` 浮点 BULK，每通道用 `filter_indices_i32` 整数 BULK
选择一组（0 起，小于 num_filters）。初始 `filter_indices` CSV 为空时全0，
非空严格要求 channels 项；非 banked 模式不接受该配置。
索引和系数均在块边界提交，切换索引不重置历史、不执行其它系数集。
reset 保留索引。多个参数槽不是自动事务：需要成组更新时，调用方须在处理边界前
完成写入并管理原事务顺序；不能假定分开的并发写入天然原子。
非法索引在 process 写历史/输出前显式报错，不钳位或读取越界系数。
`processing_enabled=0` 输出正零并冻结历史和游标；清系数则仍消费输入并推进历史，
这两种禁用语义不能混用。reset 清历史/游标，保留系数、长度、模式与处理使能。

## 混合交互的使用边界

同一个组件可以同时提供计算端口和参数接口，但这不是由框架自动选择或切换的
“混合传输”。两类声明表达不同契约，数据大小和更新频率不是分类依据。

| 接口 | 用途 | 传递和时序 | 声明位置 |
|---|---|---|---|
| 计算数据端口 | 本次算法所需的音频、频谱、向量、矩阵或判定值 | 生产者写buffer，消费者经process输入读取；sync或latest决定依赖关系 | `ports`，普通`graph.connections` |
| 参数接口 | 组件持有的配置、目标值或可寻址状态 | 经注册槽/参数回调访问，由update_policy规定局部生效策略 | `parameters`及必要的`bulk_slots` |
| 参数控制连接 | 一个组件的公开控制值驱动另一个组件的参数 | 同Task块末两相快照，下一轮计算可见，不参与同轮数据拓扑 | `control_connections`，端点须声明control_source/bindable |

例如“每帧功率谱”是数据输入；“外部设置的滤波系数表”通常是参数。
如果系数本身是本轮算法的中间结果，并要求本轮消费，则应声明为数据端口。
标量也可以是算法数据，数组也可以是参数，不能以“BULK”这个词决定交互方式。
参数的`update_policy: immediate`也不会取消控制连接固有的跨块交付时机。

### 同功能数据输入与参数共存

组件必须在接口说明和实现中明确采用哪一种规则：

- **覆盖/备用**：已连接的数据输入优先；未连接才使用参数。
- **显式合成**：例如用户增益乘以自动增益，须说明单位及合成公式。
- **互斥**：只允许选择其中一种输入方式，必须有明确配置及错误提示。

框架不根据端点名字相似自动赋参，也不会从数据大小推断优先级。
数据输入与参数存储应分离：使用数据计算有效值，不等于把数据写回参数。
如需跨两类接口转换，必须有显式的适配实现并说明交付时机；当前没有自动转换。

覆盖/备用的接口示意如下（不是声称现有gain组件已经增加了这些端口）：

```yaml
ports:
  - id: gain_in
    direction: input
    type: control
    sample_format: f32
    channels: 1
    block_size: 1
    timing: latest
    description: 线性增益；连接时优先使用，包括首次产出前的零；未连接时用gain_default
parameters:
  - id: gain_default
    type: float
    default: 1.0
    persistent: true
    bindable: true
    update_policy: block_boundary
    description: gain_in未连接时使用的线性增益；不被数据端口自动写回
```

“未连接”由`ctx.inputs[]`是否为NULL判断；**已连接但值为零**不等于未连接，
不能据此回退到参数。latest输入首次产出前为零，不能偷偷增加“非零才有效”的规则。
参数控制连接的目标只允许一个图内驱动；外部SET与控制链同时写同一参数时，
宿主仍须明确控制权，控制链可能在下一交付点覆盖外部值，不能视为自动合成。

### 接口声明对齐

1. 数据端口声明id、方向、类型、格式、尺寸、输入timing及布局/未连接行为；
   参数声明id、数值类型、shape、update_policy和控制源/目标能力。
2. `ports.type: control/bulk`不是参数控制连接；`parameters.kind: bulk`也不是计算端口。
   schema拒绝把bindable/control_source/update_policy/bulk_slot放到端口，
   或把timing/sample_format/channels/block_size放到参数定义中。
3. timing只能显式声明在输入端口；latest只适用于同Task非音频输入。
   时序属于编译/调度契约，不要求在C ABI端口结构中重复增加timing字段。
4. manifest与C ABI的descriptor、回调、注册槽须相互一致：端口id、方向、类型和格式
   不矛盾，参数/槽类型及更新策略一致；动态维度按同一组参数展开。process读取buffer
   实际尺寸，不能把ctx.frame_count当成频点/矩阵元素数。调度等额外元数据不要求在
   每一层重复声明，但不能与实际执行行为相反。
5. 复合组件公开接口应与映射后的叶子契约一致；当前执行以实际叶子声明为准，
   不能只在外层写latest或优先级说明，却让内部实现使用另一种语义。

执行细则见[非音频计算数据](reference/clock-scheduling.md#非音频计算数据2026-11)，
参数链路见[控制链路](reference/control-links.md)。

### 数据布局、分帧与互斥输入

非音频端口可声明`layout`和`frame_size`。例如半复数频谱每N槽打包一帧：

```yaml
input_alternatives:
  - [in, spectrum]
ports:
  - {id: in, direction: input, type: audio, channels: "param:channels"}
  - id: spectrum
    direction: input
    type: bulk
    channels: param:channels
    block_size: in:block_size
    layout: halfcomplex
    frame_size: param:fft_size
```

- `layout`是明确的数据布局标识；`frame_size`是数组内部的分帧长度，不是时间量子。
  帧长须为整除数组长度的正整数，声明为0时表示整个数组为一帧。参数引用允许使用
  manifest默认值。相连两端的布局和分帧声明必须完全相同，不能仅因总元素数相同
  就把3×256当成6×128，也不能把半复数当作Re/Im交织复数；框架不隐式适配。
- 输入端口的`block_size: in:block_size`继承**该输入所连源的payload长度**。
  输出端口使用此表达式时，取同步音频输入长度（音频merge取合流量子），没有音频时取
  同步数据输入长度；多个数据输入长度不同则必须显式写输出尺寸，不能任取第一条边。
  未显式声明尺寸的音频输出仍继承处理时间量子，原行为不变。
- `input_alternatives`每组必须且只能接入一个输入；未知成员或同时接入/全部悬空在
  编译期报错。实现仍须检查NULL及互斥关系；零谱不是“未连接”。
- 这些字段属于编译期接口契约，plan保留数据buffer的布局和帧长；没有新增ABI字段、
  运行模式、隐式参数写回或跨Task传输机制。复合图最终仍按映射后叶子端口检查。

验收采用`test_data_ports.py`中的真实C组件：无数据输入时使用参数，参数控制链
下一轮生效；接入数据后按声明覆盖，零值仍有效，参数本身保持独立。
这两种混合场景与纯数据场景均验证动态/生成的全局和Task四种入口。

## 周期载波的两种显式语义

`sine_mod.modulation_mode=0`（默认）保留原 AM/颤音及旧参数行为：
`y=x*(1+depth*sin(phase))`。不能把它直接用于有正负载波的纯解调。
模式 `1` 是周期表纯乘法：`carrier_table` 提供恰好 `table_length` 项
有限 float32 CSV（1..1024项），每帧只推进一次表索引，全部音频通道共享该帧载波。
不加直流偏置、不截断负载波，也不使用 `freq_hz/depth` 换算周期。
表与模式是编译/重启配置，不是运行期 CSV 参数调用；
从索引0开始、跨块连续，reset 只重置表索引，保留表和其它配置。
零载波仍执行浮点乘法，不用清零替代，因此保留负零及 IEEE NaN/Inf 传播。
表长度不匹配、垃圾后缀和非有限表项在 prepare 返回错误，不补值或截断。

## 滤波暂停不等于末端静音

`iir_bank.processing_enabled` 是可读写 RTC 整数参数（0或1），默认1保持原滤波行为。
设为0时，该次 process 输出逐样本**正零**，不读取输入、不执行递推，
不推进任一通道/级的历史。设回1从暂停前的历史继续；不是先持续滤波再乘零，
也不是用零输入继续推进状态。暂停期间仍可通过原BULK接口更新系数，
重新启用时使用新系数和保留的历史。reset 始终清历史，但保留系数和使能配置。
直接参数/ID调用即时生效，参数控制链仍按既有块末快照语义生效；
源模型的异步复位/系数事务不能仅靠一个使能位自动等价，调用集成须单独验收。

`iir_bank.stages_per_channel` 是编译/重启配置的整数CSV，空字符串保留原来
所有通道执行 `num_stages` 级的行为。非空时严格要求 `channels` 项，每项在
`[0,num_stages]` 内；空项、缺项、多项、小数、溢出及垃圾后缀均拒绝。
共享/每通道系数模式都支持不同实际级数。未使用级既不执行浮点运算也不更新历史，
不能用额外执行 unity 级代替；0级直接复制输入，保留负零和非有限值。
CSV系数仍按每通道 `num_stages × 5` 的固定跨度排列，较短通道须保留未用槽位；
运行期BULK仍是原有每通道 `IIR_BANK_MAX_STAGES × 5` 的固定物理跨度，
本配置不改变BULK容量或地址。reset清全部历史，保留每通道级数配置。

`iir_bank`、`channel_router`及`wav_in/out`支持1..64音频通道；
这不是所有组件通道上限的全局变更，其它组件仍按各自manifest校验。
IIR每通道BULK槽物理上限为5120个float，但有效形状为
`[channels,16,5]`，不能把未实例化的通道容量当作可写数据。
高位通道与低位通道使用同一递推和路由规则；默认双通道行为不变。
超出64通道的直接C配置拒绝，不静默截断为64通道。

## 直接型多项式IIR与双二阶级联

`orpheus.builtin.iir`保留1..16阶直接型II转置递推，支持1..64通道，
与`iir_bank`的SOS级联是不同组件；不能因传递函数可分解而默认互换浮点状态。
归一化`a0=1`，编译CSV严格为`[b0,...,bN,a1,...,aN]`共`2N+1`个有限float32，
不自动归一化、补项或截断。`per_channel`配置逐通道紧凑排列完整系数。
递推先计算`y=b0*x+z0`，中间状态按`(z[k+1]+b[k+1]*x)-a[k+1]*y`更新，
末状态仅计算`b[N]*x-a[N]*y`，没有额外二阶填充或多余状态运算。

共享BULK为33项；每通道BULK固定跨度33，有效shape为`[channels,33]`，
各行仅前`2N+1`项参与运算。BULK沿用块边界双bank提交，不重置历史。
将系数清零仍会输出旧状态尾样并继续移位，不等于立即静音或冻结；
`processing_enabled=0`才输出正零且不读输入/推进历史。
reset清历史但保留系数、阶数、系数模式及使能。CSV仅用于重启配置，
运行期系数更新必须经BULK接口；调用方负责滤波稳定性和系数事务顺序。

## C ABI 边界

- Runtime 通过 descriptor、ports、parameters 和 process 访问组件。
- 入口符号使用统一宏导出，避免静态链接冲突。
- 组件 process 中禁止动态分配、阻塞锁、文件网络 IO 和异常传播。

详细说明见 [[reference/registry|组件注册与 ABI 细节]]。
