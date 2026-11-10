# 统一组件包与目录工程

## 设计原则

物理目录不表达图的逻辑层次。图只引用组件 ID，Resolver 负责从工程 import、全局组件库或二进制包中找到定义。

以下节点写法对原子源码、预编译二进制和递归复合组件完全相同：

```yaml
component: external_model.spatial.fdp
```

`sub:` 仅作为旧工程和 UI 内部视图键兼容，不属于新工程持久化格式。

## 目录工程

```text
my_project/
  project.yaml
  project.layout.yaml
  definitions/
    signal_path.component.yaml
    signal_path.component.layout.yaml
    algorithms/
      fdp.component.yaml
  native/
    coefficient_solver/
      component.yaml
      CMakeLists.txt
      src/
      include/
  assets/
    fdp_coefficients.f32
  notes.md
  node-notes.json
```

目录名称和图层次没有关系；移动定义文件只需更新 `imports`。

```yaml
version: "1.0.0"
imports:
  - definitions/signal_path.component.yaml
  - native/coefficient_solver/component.yaml
  - component: company.shared.calibration
    version: 1.2.0
    sha256: 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef

graph:
  nodes:
    - id: path
      component: company.product.signal_path
    - id: solver
      component: company.algorithm.coefficient_solver
```

字符串 import 是相对 `project.yaml` 或当前组件定义文件的文件/目录。`{component: id}` 引用已安装的全局定义。相对路径不得逃出工程目录；符号链接在 containment 检查前解析。

## 组件库显示顺序

组件库顶部依次显示“工程子组件”和“工程定制组件”，默认收起，可点击展开并查看数量；
之后按[通用组件库](component-library.md)的九个功能类显示。工程导入组件仍可保留多级分类。
工程子组件只列一次，不同时混入分类树；导入的只读子组件不提供删除入口。
工程定制组件包括工程私有导入的原子组件，以及尚未提升为公共库的用户自定义组件。
搜索跨三个范围查找，收起状态不影响搜索结果；拖拽使用原组件ID，不改变工程定义。

`GET /api/projects/{name}/components` 用 `project_local` 标记工程私有定义：
以解析后的manifest来源与全局库比较，不靠ID前缀或分类名猜测；工程registry中的全局快照仍属通用组件。
工程私有组件不提供全局删除/提升操作，用户全局自定义组件原有管理入口不变。

## 从界面新建源码组件

打开工程，在「组件库 → ＋ 新建源码组件」编辑 YAML 配置。默认保存到当前工程的
`components/<包名>/component.yaml`；包名使用小写字母开头的字母、数字、下划线，
不可使用 Windows 保留名。组件 ID 在 YAML 内独立声明，不由目录名推导。
创建前先保存当前画布，生成成功后只更新工程 `imports` 和私有组件目录，
不会重载或清空画布；展开「工程定制组件」或搜索组件名，即可拖入图中。

- 「保存配置」只保存文本，不执行代码生成，也不宣称注册成功。配置可暂时不合法。
- 「保存并校验」先保存，然后使用当前 Python 解释器执行固定位置的
  `scripts/validate_component.py <配置绝对路径> --format json`。
- 「保存并生成」先保存并校验，再执行同一独立
  `scripts/generate_component.py <配置绝对路径> --format json`，不走另一套表单生成器。
- 校验、生成、注册失败分别显示诊断；已保存的配置保留，可继续修订，关闭后也可用
  「载入已有配置」恢复。失败不添加新的 import。生成器保护 `user/algorithm.*`、
  `CMakeLists.txt` 及所有权校验失败的生成区，不会静默覆盖已有算法或手工修改的生成文件。
- 成功后先验证完整工程私有 Registry，再保存相对路径 import；不会写入全局组件库，
  不构建组件、不修改 Runtime。生成的算法初始文件仅是待实现骨架。

服务接口：`GET/PUT /api/projects/{工程}/source-components/{包名}` 载入/保存配置；
`POST .../{validate|generate}` 只接受已保存配置的 `expected_sha256`，从磁盘读取。
保存已有配置同样要求上次载入/保存的摘要，以拒绝外部修改冲突。
所有写操作受工作区编辑租约保护；路径拒绝符号链接及目录联接，不能跳出当前工程。
前端异步保存/生成固定使用发起时的租约，不借用接管后新令牌继续旧操作。

编辑器提供 `type: trigger` 纯触发示例：输入用 `process: true`，输出可声明
`drive_port_no`。它没有 PCM Buffer，不填写音频通道、块长、采样格式等字段；
画布与接口审查显示「触发（无 PCM）」和真实驱动出口编号，而不是伪造 PCM 规格。
目前前端只允许普通 `trigger → trigger` 连线；跨到 audio 等其它类型时显示中文拒绝原因，
不自动转成音频边或旧 `drv:out/in` 虚拟执行边。

## 生成壳的资源生命周期

声明 `algorithm.lifecycle` 的组件由生成壳调用对应钩子，资源只在生命周期中分配和释放，
不能在实时 `process` 中申请内存。调用者提供对齐的 State，仍须先 create 再 prepare。

prepare 先校验候选参数；校验失败保留旧参数和资源。校验成功后先清理旧资源，
提交新参数，再调用算法 prepare；资源重建失败会再次清理并返回原错误，
**不回滚旧历史**。此时不能继续实际处理，须成功重试 prepare 后再运行；
destroy 必须能安全清理失败或尚未 prepare 的实例。
bulk_one_pole 与 bulk_guarded_slew 的原生验收覆盖分配失败、重试、
reset/destroy 及处理期间零内存分配。

## 接口化通道选择与索引资源

channel_router 的 `in` 是驱动入口，`out` 是 1 号出口；空索引 CSV 表示顺序映射，
非空恰好要求输出通道数个 int32，不再容忍垃圾字符、静默截断或不足项补齐。
负值和越界置正零，有效通道按位拷贝。

channel_selector 的 a/b/base 都是处理入口：每次实际交付立即处理当前可见输入，
不等待其它入口，也不合并同一 Task 的多次调用。`indices/gains` 字符串是设计初值，
分别绑定私有 int32/f32 BULK；运行期按真实数量整组写入，双 Bank 在 Task 边界提交。
reset 保留运行期索引和增益。缺失输入或无效索引不做乘法，输出正零或原样保留 base；
禁用分支时同样保留 base。两组件都拒绝输入输出重叠，零量子不交付下游。

## 常量表与查询调用点

bulk_constant 的 tick 是纯触发入口；out/first/last 分别是 1/2/3 号出口，
算法依次同步交付完整表、首项和末项。出口悬空仍可继续其它出口，
任何下游错误会终止本次后续交付。CSV 容量为 65536 字节，表长为 1..2048 项，
严格校验数量与有限数值；reset 保留常量表。

bulk_lookup 的 x 是唯一处理入口，axis/values 是 `timing: latest` 侧输入。
表必须在实际查询前有效，但不要求编译器证明上游不同出口的调用顺序；
先更新表还是先发查询由上游算法的实际交付点决定。表更新本身不触发运算，
查询时检查有限严格单调轴，升/降序都支持，越界或 NaN 查询输出正零。
两组件都拒绝输出别名，零量子不交付下游。

## 基础运算与返回后反馈

scalar_math 的 a/b/c、bulk_math 的 a/b 都是独立处理入口，
每次实际交付立即读取当前两侧数据，不等待另一入口；
纯触发 tick 支持无数据连接时使用配置常量。
为避免统一接口同名，标量常量参数为 `a_default/b_default/c_default`，
数组常量参数为 `a_default/b_default`，不保留旧 a/b/c 参数别名。
数组载荷长度与 Task 量子无关；模长保留原分支及逐步 f32 运算，
不以 `hypotf` 改变 NaN、Inf 或溢出语义。

scalar_memory 仅 tick 驱动，in/update/initial 均为 latest 侧输入，
enabled 是非驱动门控输出。初值参数为 `initial_value`，与 initial 数据口分开。
默认模式先读取并提交输入，再交付下游；下游失败不回滚已提交值。
`write_after_call` 模式先交付旧值，仅在下游成功返回后重新验证并读取当前输入，
失败不提交历史；门控为零不读取输入。reset 清除历史，动态修改初值仅影响尚未初始化的延后模式。

配置字段 `allow_unready: true` 只允许用于**非驱动、latest 的 BULK 输入**，
用于这类返回后才读取的反馈，不是通用的校验绕过。
生成壳仍验证格式、通道、容量、指针及对齐，只额外接受 frame_count 为零，
或等于声明的完整载荷长度；中间长度拒绝。算法实际读取前必须验证数据已完整就绪，
续调返回后重新读取 Buffer 当前指针和元数据，不沿用进入算法时的缓存视图。
普通 latest 输入没有此放宽；不因未就绪而补调、等待或重排。

delta_hold 的 in 实际交付驱动 1 号 i32 BULK out；
每通道比较严格增量，保持计数按 batches 内每批推进，而非 Task 帧数。
prepare/reset 清除历史，动态修改 threshold/hold_ticks 不清历史；
NaN 比较为假，仍照常推进保持规则。输出悬空仍更新历史并交付，
下游失败不回滚已完成的递推；错误缓冲区和零量子不推进状态。
历史固定存于调用者状态区，无隐式堆状态或 process 分配。

dual_trapezoid_smooth 由 in 实际交付驱动，先续调 1 号 fast，
成功后再续调 2 号 slow；任一悬空出口仍推进全部历史，不阻断其它出口。
每批共享并顺序更新上一输入、两份输出历史，保持梯形递推的分步 f32 舍入；
非零 initial 在首次有效处理时初始化，之后修改 initial/alpha 不清历史。
prepare 分配私有历史，reset 清初始化标记，destroy 释放；
错误或零量子不初始化，全部输出重叠检查在初始化与写入之前。
两份递推在交付前均已完成，下游失败停止后续出口但不回滚算法历史。

trapezoid_ratio 的 numerator/denominator 都是处理入口：
每次实际交付立即读取两侧当前完整数组，推进四组历史并交付 1 号 out，
不等待另一入口、不按同拍去重。调用方负责准备可读侧输入；
缺失或不完整的数据明确拒绝，不补零或延迟补调。
保留直接除法与先倒数再乘的不同 f32 舍入和 IEEE 行为；
epsilon 必须在 [0, FLT_MAX] 内，alpha 必须在 [0, 1] 内。
动态修改参数不重置历史，NULL 输出仍推进并交付，
reset 清历史，下游失败不回滚，process 不分配内存。

bulk_ring_delay 的 in 实际交付驱动 1 号 out。
每批先完整写入环形池，再按本批起点减 delay_elements 取模读取；
这不是逐样本延迟，也不以 Task 帧数解释偏移。
容量必须不小于 bins；动态改偏移不清历史，跨多圈偏移仍按容量取模。
prepare 分配池，reset 清池和游标，destroy 释放；
NULL 输出仍推进并交付，零量子不推进，下游失败不回滚游标或历史。
读写仅复制位型，全部缓冲区/别名检查先于写池。

## 复数运算与频谱载荷

complex_power 的 in 驱动逐批功率及能量计算；按实际算法调用点依次交付
1 号 power、2 号 energy。complex_gain_split 的 in 驱动，
gain 是不触发运算的 latest 数组；依次交付 1 号 impulse、2 号 reverb。
两组件保留分步 f32 舍入，不融合乘加/乘减，也不以批次数改变 Task 时间线。

halfcomplex_encode 逐位复制端点实部及内部实虚部，
不执行 FFT、缩放或隐式补齐；bins 必须等于偶数 fft_size/2+1。
1 号 out 使用 audio 类型 HC 载荷，2 号 spectrum 使用带 halfcomplex/frame_size
标签的 BULK；两出口独立交付，任何下游失败停止后续出口。
全部输出先校验再写，拒绝与输入或其它输出重叠。

halfcomplex_decode 的 in（audio）和 spectrum（带布局的 BULK）互斥，
任一入口实际交付立即解包，不等待或合并。输出可截取连续频点，
实部/内部虚部逐位复制，DC 和 Nyquist 虚部固定正零。
先交付 1 号复数 out，再交付 2 号 i32 BULK 成功标记 tick；
tick 仍是数据而非无载荷 trigger，下游失败立即停止后续出口。

complementary_coefficients 保留每行前 N 通道的原始位型，
后 N 通道执行一次 total-input 的 f32 减法；in 驱动 1 号 out。
输出 channels 使用 `param:channels*2`，由同一有界乘积规则展开，
不引入重复的输出通道参数；32 通道×65536 项实际展开为 64 通道。
两组件均使用调用者提供的状态区，零量子不续调，
输出悬空不阻断其它出口，重叠在写样本前明确拒绝。

这些接口的 block_size 是完整载荷槽数，可以是多个有界签名参数与正整数的乘积，
例如 `param:bins*2*param:batches`，不能将结果当成新的时间样本数。
显式声明的 audio 载荷也与调用量子分离；普通没有显式载荷的 audio 接口仍按调用量子处理。
布局标签属于静态接口契约，不通过不同出口的编号推断调用先后。

## 统一组件定义

所有定义使用组件 manifest。实现差异只体现在 `package_type`：

```yaml
id: company.product.signal_path
name: 信号链
category: 工程/信号链
version: 1.0.0
abi_version: 1
package_type: composite # source | binary | composite
```

### 复合实现

```yaml
ports:
  - id: in
    direction: input
    type: audio
    channels: 2
    maps_to: filter:in
  - id: out
    direction: output
    type: audio
    channels: 2
    maps_to: gain:out

parameters:
  - id: gain_db
    name: 增益
    type: float
    direction: input
    maps_to: gain:gain_db
    default: 0.0
    update_policy: smoothed

graph:
  nodes:
    - {id: filter, component: orpheus.builtin.biquad, params: {channels: 2}}
    - {id: gain, component: orpheus.builtin.gain, params: {channels: 2}}
  connections:
    - {from: filter:out, to: gain:in}
```

复合组件可以引用另一个复合组件。边界只能映射到直接子实例，但 Resolver 会递归穿透到原子节点。循环引用在 flatten 阶段报错。

### 源码与二进制实现

源码组件沿用 `sources`、`headers`、`deps` 和 `CMakeLists.txt`。工程 import 的源码目录通过 `ORPHEUS_EXTRA_COMPONENT_DIRS` 加入现有构建，目标名称仍为组件 ID 点号替换为下划线。动态运行和代码生成使用同一份项目 Registry。

二进制组件沿用 `binaries[]`。对父图而言，两者与复合组件没有区别。

## 外部资源

大系数不写入 YAML：

```yaml
resources:
  reference.headrest.fir:
    file: assets/headrest.f32
    format: f32le
    shape: [10, 1280]
    sha256: 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef

graph:
  nodes:
    - id: fir
      component: orpheus.builtin.fir
      params:
        channels: 10
        coefficients: {$resource: reference.headrest.fir, offset: 0, count: 1280}
```

支持 `f32le`、`json`、`csv`。加载时校验 hash 和 shape，再转换为现有 BULK 参数；序列化时仍保留 `$resource`。因此动态路径和代码生成路径继续消费同一个 plan 数值。

参数引用可带 `offset/count`，让多个基础组件共享一个大资源文件而不复制数据。

## 加载与保存

1. 加载全局 Registry 快照。
2. 从 `project.yaml` 递归解析 imports。
3. 检测重复组件 ID、路径逃逸、缺失定义、全局版本与 manifest 哈希。
4. 将复合定义物化为内存图。
5. flatten 为原子 Project 后交给现有 Compiler。

API GET 会物化复合定义供 UI 编辑；PUT 按组件来源写回各自 manifest。新建的带命名空间组件自动保存到 `components/<id>.component.yaml`，不会回填进 `project.yaml`。

### 结构与画布布局

加载支持两种位置来源：原有节点/访问桥的内嵌 `position`，以及独立布局文件。**今后保存默认使用独立布局文件**；旧工程在下次保存时迁出坐标，不需要提前批量迁移。

结构文件仅保留布局引用：

```yaml
layout:
  file: project.layout.yaml
```

布局文件以视图和节点 ID 定位，不影响连线、参数、调用顺序或执行计划：

```yaml
version: 1
views:
  main:
    nodes:
      source: {x: 40, y: 80}
    bridges:
      device: {x: 400, y: 80}
  sub:legacy:
    nodes:
      filter: {x: 120, y: 60}
```

- 默认命名为 `project.layout.yaml`、`foo.component.layout.yaml` 或 `component.layout.yaml`，与所属结构文件并置；也可以通过 `layout.file` 指定所属目录内的相对路径，不允许越界或覆盖结构文件。
- 可写复合组件的默认布局写入自身旁文件，父工程和嵌套工程共享；只读/公共组件的位置调整写入当前工程布局的 `sub:<组件ID>` 视图，不修改公共定义。
- 外部布局或显式内嵌 `layout: {version: 1, views: ...}` 优先于旧 `position`，未列出的节点回退到原位置；空坐标 `{}` 表示清空位置。布局中不存在的节点、访问桥或子图视图直接忽略，不会新增对象，也不会导致加载失败；下次保存时清理这些失效记录。
- 内存/API 继续提供节点 `position`，UI 无需感知存储格式。缓存监控结构及布局文件，外部修改布局后重新读取即可生效；引用丢失、格式错误或非有限坐标会明确报错，不静默忽略。
- 首次迁移可能规范化结构字段；之后仅移动节点时，结构 YAML 的字节、注释和修改时间保持不变，组件 manifest 的结构哈希也不随拖动改变。图连线、驱动连接、任务、参数和说明等仍属于结构。
- Python `ProjectLoader.save(..., separate_layout=False)` 可明确选择内嵌保存。完整工程 ZIP 包含布局旁文件，复制工程时应一起保留。

## 不变边界

- Runtime、C ABI 和 execution plan 不感知复合组件。
- CodeGenerator 仍只处理 flatten 后的原子图。
- 节点展开 ID 继续使用 `instance__child`，保持参数 ID 和 arena 布局稳定。
- 工程私有 Registry 与其他工程隔离；重复 ID 不允许隐式覆盖。