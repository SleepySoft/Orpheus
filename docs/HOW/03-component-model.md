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

## C ABI 边界

- Runtime 通过 descriptor、ports、parameters 和 process 访问组件。
- 入口符号使用统一宏导出，避免静态链接冲突。
- 组件 process 中禁止动态分配、阻塞锁、文件网络 IO 和异常传播。

详细说明见 [[reference/registry|组件注册与 ABI 细节]]。
