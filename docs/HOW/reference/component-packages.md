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

需要钳位或同步关联参数时，可声明纯候选钩子
`normalize(Parameters* parameters, const char* key)`：
完整配置的 key 为 NULL，运行期设置则为本次字段名。
生成壳先检查输入类型和声明范围，再规范化候选，重新检查所有标量、
数组及 UTF-8 字符串约束，最后调用只读 validate；全部成功后才接触既有状态。
因此需要接受越界整数请求再钳位的接口不应把请求范围声明为目标范围。
normalize 不得访问既有实例或外部状态，也不得保留候选指针。
规范化产生的关联字段与本次字段一起提交，不会被 set_parameter 钩子后的
旧状态快照覆盖；它们优先于可变钩子对相同字段的修改。
未被候选改动的其它字段仍保留可变钩子更新。失败保留旧参数和算法资源。

资源接口的 `shape` 可使用 `config:frames`，表示节点配置的处理帧数
（ABI config.block_size / plan cfg.frames），不表示所属 Task 的块长，
也不由显式音频载荷长度替代。例如 `[config:frames, param:channels]`
描述节点处理量子的交错音频资源；固定物理容量仍由 BULK 的 count 声明。
生成壳在接触旧状态前拒绝零配置帧数和超过容量的形状，并避免维度乘法溢出。
内部配置帧数随参数快照和提交保存，运行期设置及 reset 不会丢失；
normalize 不得修改它。其它 config/task 引用以及非资源形状仍不支持此维度。

音频端口可声明 `host_binding: {slot: samples, access: write, status: ready}`。
slot 引用外部 BULK 资源键（可不同于 C 接口名），status 引用 bool 标量参数；
write 要求可写存储及可读写提交状态，read 要求可读存储及可读有效状态。
此绑定只描述宿主供数/取数，不创建驱动调用；缓冲驱动源的输出也可绑定 write。
仅支持交错 f32，可用 `when: {mode: 0}` 选择配置：
条件必须引用 restart_required 标量配置，值须满足严格类型、范围及字符串容量。
独立校验工具与图编译共用静态契约校验，未激活绑定也不能引用无效资源；
图编译另检查活动绑定互斥、存储形状与端口签名，已接上游的输入禁止宿主供数。

block_split将大块发布与小块读取分成两个真实入口：in只快照，
tick才消费并交付。output_frames显式声明读取长度，factor只决定输入形状比；
欠载和未读覆盖报错，水位不驱动执行。旧工程必须明确连接读取触发，
不保留按调用次数猜测大块到达及启动补静音的路径。
具体存储、时间和错误语义见[拆块器](../../../components/orpheus/builtin/block_split/README.md)。

downrate反向组块同样采用两个真实入口：in只追加input_frames帧，
tick独立读取factor倍完整大块并交付；factor不是调度分频，写满不调用下游。
部分批次要求时间连续且epoch相同，拒绝中途DISCONTINUITY及部分批次EOS。
显式端口载荷不覆盖节点处理量子，大块消费者须声明对应的载荷形状。
具体契约见[组块器](../../../components/orpheus/builtin/downrate/README.md)。

resample同样分离写入和读取：in跨调用保留平均/选点分组状态，只写私有存储；
tick读取完整output_frames并显式交付低速音频，写满不自动续调。
input_frames须整除output_frames×factor，批次输入起点须对齐factor；
输出绝对帧位置除以factor，保留epoch并聚合质量标志。
WAV源在音频out子树成功后发出2号纯触发tick，可经触发分频器驱动读取。
参考[整数抽取契约](../../../components/orpheus/builtin/resample/README.md)及
[WAV示例](../../../examples/wav_resample.yaml)；采样率表达式不替代真实触发。

## 接口化通道选择与索引资源

indexed_tone 的in唯一驱动，每次到达自主处理并交付1号out。
索引模式保持跨unity两阶段、逐样本系数支路选择及块末吸附；
显式模式保持块起始增益选支路、逐样本递乘、阶段计数和INT32_MIN饱和，
一阶反馈符号及二阶运算顺序不与索引模式合并。
freeze只停包络，滤波继续推进；下游失败不回滚。
显式零帧不推进，索引零帧仍执行既有块末计数/阶段切换，两者都交付事件。
reset均清滤波，索引模式取消剩余斜坡但保留增益/目标，
显式模式保留阶段及包络。两个五项f32双Bank系数资源绑定私有存储；
两种初始化模式统一要求完整有限CSV，不再接受部分解析或静默补值。
精确原地合法，部分/状态重叠与不足载荷拒绝，分配声明仍保守禁原地。

n_way_mux 的全部可变输入都是处理入口，每次真实到达独立读取当前可见候选，
推进位置并自主交付1号out，不增加会合等待或同拍合并。
稳态逐位拷贝所选输入，未接候选正零；过渡位置按逐样本步长跨过相邻输入，
不改成旧/新两路直接交叉淡化。normalize同时钳位并提交select/select_index，
保留完整int32请求和初始索引优先，不在setter中依赖会被壳覆盖的当前字段修改。
reset吸附目标，零帧仍交付；平滑模式不推进，立即模式仍吸附。
下游失败不回滚位置；select_pos只读，不足载荷及输入/输出/状态重叠明确拒绝。

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

psd 的普通 audio in 按实际交付量子处理，分析后原样交付 1 号 out，
不把 PCM 块声明为固定频谱载荷。保留原 radix-2 FFT、幅度 EMA 和通道 0
到 1023 点 spectrum 的线性映射，不新增窗或改为平方功率。
仅实际块长为 2 的幂且匹配 prepare 尺寸时更新分析历史；
其它块和短交付只旁通，不更新探针。精确原地和部分重叠均先分析后复制，
下游失败不回滚已更新的历史；reset 清历史及探针，destroy 释放私有 FFT 缓冲。

接口参数仅在 `kind: probe` 且 `access: read` 时允许省略 default，
独立校验/生成工具按类型及 count 派生零值，用于只读分析结果而非可写设置。
显式提供 default 时仍严格检查类型及完整数组长度，不接受标量广播；
其它参数仍必须声明 default。

spectral_reduce 的 in 按实际 audio 量子驱动 1 号 out。
仍只遍历 num_frames 内每帧前 bin_count 个频点，超出实际交付长度的点跳过；
短块、尾部不完整帧及额外尾部广播行为保留，不强制完整 FFT 批次。
bin_count 为 0 或超过 fft_size 时仍使用 fft_size/2+1，
参数回读保留配置值，不将该有效频点数回写为用户配置。
sum/mean 保留逐次 f32 累加顺序，min/max 保留 FLT_MAX/-FLT_MAX 初值及
NaN 比较为假的原语义；结果广播到整个实际输出块。无历史或 process 分配，
缺失输入/输出、数据不足或重叠明确拒绝；零量子不续调，下游失败原样返回。

window 每次 in 到达均从系数开头加权，显式交付 1 号 out；
single 超窗长部分仍乘 1，repeat 按 window_size 重复，不跨调用保存窗位置。
保留 double 解析后转 float 的初值舍入、不足窗长补零及空列表首项 1 的规则；
支持逗号或空白分隔，非法文本、重复/尾部逗号或超窗长列表明确拒绝，
不再静默截断。保留 IEEE 系数与乘法，不跳过乘 0 或乘 1。
支持精确原地，部分重叠在写样本前拒绝。
reset 仍将实际系数改为首项 1、余项 0，并切回 single，
不是恢复初始化 CSV；prepare 则重新载入配置。
coefficients 绑定唯一 f32 BULK 槽，count 为 window_size，支持双 bank 块边界更新；
window_size 因此声明 affects_signature，不改变 audio 的实际量子。
初始化 CSV 与运行期系数槽共用一个资源事实源，process 无内存分配。

probe_waveform/probe_spectrum 也在观测完成后显式交付 1 号直通 audio，
不因累积窗口未满就阻断音频，不增加频域算法数据端口。
waveform 仍是第 0 通道的原始 1023 槽环形数组，不按读取时刻重排；
prepare 清空，reset 保留原历史和游标。spectrum 保留向上取 2 的幂的
64..4096 窗长、Hann 窗、满窗才更新观测、幅度 FFT 及 1023 点线性映射；
reset 清历史及探针，prepare 重建私有资源，destroy 释放。
两组件均先观测后 memmove，支持精确原地/部分重叠；无输出连接仍更新观测，
下游失败不回滚历史，零量子不续调。
只读 PROBE 的整组读取使用 form=BULK，Runtime 与生成代码均支持名称和数据 ID，
不能因 kind=PROBE 而拒绝读取；整组写入与普通参数写入仍拒绝。

noise_slew 的 in 每次到达均按通道推进逐样本历史并交付 1 号 out，
保留原来的减法、正负增量选择、上下限比较和加法顺序及 NaN 传播。
prepare 按采样率计算每样本限幅，reset 只清历史；
修复运行期 rise_rate/fall_rate 仅修改回读值却不更新实际限幅的旧问题，
两条执行路径均通过参数 hook 更新派生缓存，不清历史。
未提供采样率时仍沿用每样本 0.0001 的原规则。
精确原地合法，部分重叠、缺失输入/输出及不足交付明确拒绝；
零量子不续调，下游失败不回滚已递推状态，无隐式堆状态或 process 分配。

audio_bulk_snapshot 的 in 到达后直接复制当前音频交错块，再显式交付 1 号
bulk out；输出载荷长度引用输入块长，不把元素数变成新的时间量子。
elements 保留每通道期望帧数的含义，0 跟随实际音频；不匹配即拒绝，
不攒块、不插入一块延迟、不隐式改变音频/计算数据类别。
所有 f32 位型原样复制，包括负零、NaN 载荷、Inf 和次正规数；
快照与输入存储独立，原地及部分重叠拒绝。未连接输出只验证输入，
零量子不交付，下游错误原样返回，无 process 分配或内部历史。

spatial_enhancer 保留同帧 M/S 分解、三旋钮逐样本平滑、原侧声一阶
高通系数与递推，以及交叉混合的 f32 运算顺序；每次 in 到达后直接
交付 1 号 out，不插入块延迟。width/air/mono_mix 更新只改目标值，
下次样本继续平滑；reset 将平滑值归到当前目标并清高通历史，
prepare 重载配置及采样率系数。声明为 sample_rate_independent: false，
不因旧 manifest 的 independent=true 错误标注而绕开计算。
沿用不支持原地的公开契约并明确拒绝部分重叠；
不足/缺失载荷与越界或非有限参数在写入前拒绝，不再静默夹参数。
零量子不交付，下游失败不回滚历史，状态由宿主提供。

switch 按 enable>=0.5 决定开关目标，保留原指数斜坡的 f32 顺序；
斜坡按交错样本推进，**不是每音频帧一次**，迁移不调整多通道的斜坡速度。
每次 in 到达后直接交付 1 号 out，修改 enable 不清当前增益，
ramp_ms=0 或采样率为 0 时沿用立即变化规则。
reset 保留原来的特殊语义：恢复 enable=1 和增益=1，保留斜坡系数，
而非恢复初始化 enable；prepare 才重新加载配置。
声明为 sample_rate_independent: false；精确原地合法，
部分重叠、缺失/不足输入输出及非法候选在写入前拒绝。
零量子不交付，下游错误不回滚当前增益，无隐式堆状态或 process 分配。

mixer 的 in0/in1 均为驱动入口，每次到达读取两路当前输入，
逐点加权相加并显式交付 1 号 out；不等待两口会合、不做同拍去重。
两输入均必需，允许两输入共享存储，但输出不得与任一输入重叠。
保留原来的逐样本 `current += 1*(target-current)` 及两次乘法后相加；
不简化成直接赋值，避免改变小增益首次到达时的 f32 舍入。
prepare 从单位当前增益和配置目标开始，运行期调参仅更新派生目标，
reset 恢复两路单位增益/0dB。gain0/gain1 统一回读配置目标，
不再混用旧 get_parameter 的已应用增益和注册槽的目标 dB；
reset 同步回读 0dB，保证配置事实源与实际算法状态一致。
零量子不交付，非法载荷/候选在写入前拒绝，下游失败不回滚已更新增益。

sine_mod 保留两种不同的推进语义：AM 按交错样本推进 f32 相位，
周期表按音频帧推进游标、全部通道共用同一表值。
运行期调频同步更新相位增量但不清相位；表模式不使用频率/深度。
严格 CSV 仍要求恰好 table_length 项，保留 strtof 的舍入与 ERANGE/
非有限检查，AM 不解析不用的表文本。表模式 reset 只清游标/相位，
保留表和参数；AM reset 恢复 1Hz/深度 1 并清相位，
同时修复原来回读恢复 1Hz、相位增量却仍是旧频率的缓存错误。
精确原地合法，部分重叠与不足/缺失载荷拒绝，零量子不交付；
每次有效 in 到达后直接交付 1 号 out，下游失败不回滚相位/游标。

fade 的in是唯一驱动入口，lpf0只提供当前可见低频；
数据口不额外推进算法，也不增加会合等待，工程负责先发布低频再驱动主输入。
内部模式保留低通历史、前后增益平滑及front_channels初始化夹取；
外部模式四组增益按块起始值乘逐样本幂次，仅前两计数控制对应高低频组，
后两计数只存储，INT32_MIN饱和，freeze不吸附目标或递减。
四个数值资源绑定私有存储，remaining_blocks为i32。
每次主输入到达自主交付1号out；零帧保留历史并交付事件，
下游失败不回滚。内部reset只清低通历史，外部reset保留显式资源。
出口编号不保证先后；同口扇出依工程保存顺序同步完成子树。

limiter 的in每次真实到达都处理并自主交付1号out，不按Task去重。
shared按交错样本推进单包络，per_channel保留各通道历史与最后值广播，
feedback保持两组×channels的四个f32双缓冲资源、参数组切换和旁路冻结。
active_mask保留32号通道的符号位，gain/active_mask只读。
零帧保留历史及旧位图、仍交付事件；下游失败不回滚。
合法逗号/空白系数列表及strtod舍入保留，垃圾、非有限、超项明确拒绝，
不再让无效文本反复解析为零；反馈CSV仍要求完整两组或空串默认。
精确原地合法，部分/状态重叠及不足载荷拒绝；
本批保留原运行期时间系数固定48kHz的已知限制。

loudness_normalizer 在每次真实音频到达时按交错样本顺序计算联合均方，
保持原冷启动能量建立、检测/增益递推、静音门及块内线性步进；
每帧先加一步增益，再处理各通道，不改成直接应用块末增益。
三个探针只读，初值与实际reset的-120dB统一；精确原地合法，
零帧不推进历史但交付零帧事件，下游失败不回滚状态。
NaN/Inf按原浮点语义传播，不加静默修复。

loudness_gain_control 保留原冷启动能量建立、逐项f32检测/增益递推、
能量下限、静音门和快压慢抬。只生成控制量，不施加音频增益；
音频每次真实到达独立推进，再自主交付1号out，逐位保留输入。
三个状态探针只读；控制链仍有显式块延迟，不改为隐式同步读写。
零帧保留原系数1的控制更新并交付零帧音频，不统一早退；
reset清历史、保留配置，下游失败不回滚状态。

wav_in 是文件时钟根，Task调用源，源从prepare缓存逐位拷贝到输出，
自主交付1号out。PCM16/32保持原归一化，f32保留NaN/Inf/负零；
多余输出通道重复文件第0通道，文件多余通道丢弃。尾部按原量子补正零，
零帧交付零帧事件、不推进；reset只复位游标，下游失败不回滚消费。
读取按RIFF块长度及填充扫描，不假定44字节头；支持附加块及fmt扩展字节。
所有长度、声道/位宽、块对齐和除数在分配/解码前检查，
不支持的压缩/位宽明确报错，不再成功返回静音。保留不重采样的原语义。

level_tracker 以peak为唯一统计事件入口；rms、volume、fast_trigger和volume_gain
是当前可见数据，不触发、不等待。快速触发输入改名为fast_trigger，
与参数trigger区分；双dB检测器先准备两个统计值再发布峰值事件。
保留原双精度保持计数、严格超过保持门限、f32衰减/夹取次序及冻结/冷启动语义；
三种探针只读。先填完两个输出，再交付level（1号）和envelope（2号），
标量载荷不改变时间量子；零帧不推进、不交付旧值。

gain_ramper 保留块级dB斜坡、逐样本递乘和帧内幂次三种算法，
四组双缓冲数值资源由声明绑定到私有存储，活动shape为num_rampers，
物理容量32；剩余计数保持精确i32，模式2在INT32_MIN饱和。
音频每次真实到达直接处理，先准备两份输出，再交付out（1号）及可选gain0（2号）。
观测是所选声道块末已应用增益；旁路1、模式2静音0，不是目标值。
明确拒绝非法映射文本，取代原256字节截断/atoi静默回退；
有效的模式0负数旁路、越界组回0、缺失项补0保持不变。
两组件下游失败均不回滚历史，出口别名在更新前拒绝。

level_detect 由每次音频in到达驱动；平滑模式保留原逐项峰值/RMS包络，
统计模式保留从首样本起的每通道均方、声明顺序归约及NaN位置语义。
level/rms_db/peak_db 明确只读；依算法调用点交付音频out（1号）、
统计rms0（2号）和peak0（3号），1帧统计载荷不改变时间量子。
统计模式允许音频出口不接；零帧不推进或交付旧缓存；
下游失败不回滚状态，全部输出别名在更新前拒绝。

signal_gen/sweep_gen 是真实合成时钟根，Task 调源，源自主交付1号out。
sample_rate 参数决定输出签名；duration_s 仍由宿主决定主动推进时长。
保留正弦源逐步f32相位及严格大于2π时仅减一次、
扫频源双精度时间/相位与结束静音；幅度更新不重置时间或相位。
两者运行期仅amplitude可写，且保持立即赋值，不增加隐式平滑。
扫频进度/频率为真实只读探针；下游错误不回滚时间/相位。

one_pole_envelope 仅由音频 in 驱动，其余四个输入是当前可见数据，
不等待会合；每次从 start 独立递推，无跨调用状态。
保持逐项 f32 乘加，freeze 非零保持起点，alpha 不夹取。
先计算全部结果，再按算法调用点依次交付 out（1号）、
gains（2号，1帧载荷）和 audio（3号，原音频逐位透传）；
载荷帧数不改变图时间量子。下游失败不继续后续出口，
允许出口未连接，拒绝全部输入输出及出口间重叠。

indexed_gain 的索引/dB表是各2048字节、最多32项的初始化文本；
要求等长、索引非递减，允许重复端点，不截短或隐藏非法输入。
保留原始查表、偏移夹取、dB上限与指数渐变顺序；
每样本先乘系数，每次实际调用末尾减块数，末次输出后才吸附目标。
冻结同时停止递推/计数，reset 只取消斜坡，保留当前增益与配置。
有效零帧事件仍消耗一次未冻结的块计数并交付；
current_gain 只读，下游错误不回滚状态。

piecewise_soft_clipper 保留二次曲线逐项f32舍入与按通道峰值比较；
首样本NaN使该通道不置位，后续NaN不更新峰值；
32位active_mask按int32原位型回读且不可写。
旁路逐位复制，reset 只清位图；有效零帧事件仍重算位图并交付。
两组件均每次in真实到达直接交付1号out，精确原地合法，
拒绝短载荷、部分重叠和State别名，非法候选保留全状态。

pool_delay 的全部通道先读取旧共享池，再按通道声明顺序写回；
位置由当前延迟前缀和决定，允许地址重叠，后通道写覆盖先通道。
变长重新解释旧池，不等同于独立通道延迟线。
delays_i32 为真实双缓冲资源，活动数量为 channels，
槽范围按实例限制为 [0, pool_size_samples]；纯候选失败保留全状态。
禁用时逐位输出干声并冻结整个池/游标，reset 只清池与游标。
每次有效 in 到达显式交付 1 号 out，下游失败不回滚历史；
保留有效零帧事件交付语义，拒绝短载荷、重叠及私有池别名。

iir 保留 1～16 阶直接型 II 转置多项式递推，不转二阶节；
最多 64 通道，每通道独立历史。共享/逐通道系数现在是两份明确的独立资源，
coefs 固定 33 项，coefs_per_channel 为 channels×33 项，均双缓冲注册，
不再让目录声明两个键、运行时却仅注册其中之一。
coefs 初始化 CSV 保留原模式含义；initial_coefs_per_channel 非空时
为逐通道资源提供独立初始化入口，在 per_channel 模式覆盖 coefs 初值。
每组紧凑 2×order+1 项展开到固定 33 项步长，纯校验失败保留全状态。
运行期只读取当前模式资源，非活动槽写入不影响活动系数；
系数更新不清历史，reset 只清历史并保留使能。
processing_enabled=0 输出正零并冻结状态，不要求输入数据。
每次非零到达显式交付 1 号输出，同 Task 不去重，下游失败不回滚历史；
拒绝短载荷、输入输出重叠及输出覆盖私有状态，零帧不交付。

fir 的系数是初始化文本，不是原来仅打标签但未注册的 BULK 槽；
容量为 32768 字节，最多 1024 抽头，taps 是派生只读探针。
空/全空白明确表示单抽头 [1]，非空非法文本不再退化或截断，
纯校验失败保留系数、每通道历史与游标。
保留 strtod 后转 f32、最新样本先写历史、按抽头顺序逐项 f32 累加，
零系数也参与乘法，NaN 历史不会被当成未路由输入跳过。
每次非零到达自主交付 1 号输出，同 Task 不去重，下游失败不回滚历史；
reset 清历史/游标并保留系数。支持精确原地，拒绝短/缺失载荷、
部分重叠和输入输出覆盖历史存储；process 不分配内存。

interp_lut 的两轴是初始化字符串，不是原来仅打标签但未注册的 BULK 槽；
现按真实所有权声明，各有 8192 字节容量，表最多 256 点。
两轴非空等长，X 严格递增，数值及相邻差值必须有限；
不再静默截短、吞垃圾或补默认表，纯候选失败保留旧状态。
x 可写，y/history 是只读探针。每次实际到达先按原 f32 次序插值，
记录最近 64 项历史，再显式交付 1 号音频出口；
有效零帧 Buffer 仍是控制事件，同 Task 重复到达不去重。
历史保持 %.4g JSON 表示，prepare/reset 使用有效空数组 []，
reset 清 x/y/历史但保留表。音频逐位直通并允许精确原地，
部分重叠和短/缺失载荷拒绝，下游失败不回滚已记录结果。

output_router 保留固定 32 项行步长和单位阵底图，短数值列表只覆盖首项，
未指定位置仍是原单位阵值；reset 保留当前矩阵。
正负零增益均跳过乘法，未路由的 NaN/Inf 输入不会污染输出，
不能简化为 matrix_mul 的全项累加/短初值补零语义。
文本容量扩为 32768 字节以容纳完整 32×32 系数，不再静默截断；
非法/非有限/超范围数值、空项、尾逗号和超过活动矩阵大小明确拒绝，
纯校验失败保留旧矩阵。每次非零输入到达显式交付 1 号输出，
不支持输入输出重叠或输出覆盖矩阵存储。

matrix_mul 保留逐列 f32 乘法/累加顺序，以及 1024 项行优先物理矩阵；
同名 BULK 槽仅注册 rows×cols 活动项，支持双缓冲整组更新。
默认初值仍是固定四项文本，非空短初值补零；空文本和 reset 使用单位阵。
初始化文本容量为 32768 字节，继续 strtod 后转 f32，不将初值文本冒充活动回读。
非法/非有限/超范围数值、空项、尾逗号及超过 1024 项明确拒绝，
纯校验失败保留旧系数。每次非零 in 到达显式交付 1 号 out，
不按 Task 去重；缺失/短载荷、输入输出重叠及输出覆盖矩阵存储拒绝。

input_select 保留 1 起点基础映射及块边界覆盖：覆盖值 0 沿用基础映射，
非正基础值、负覆盖值和越界索引置正零；短列表保留其余默认/基础通道。
基础默认文本改为空，表达同样的顺序映射，避免单输出配置继承多余默认项。
两张表容量仍为 256 字节；非法 CSV、额外项目、尾随逗号、int32 溢出或
字符串超长明确拒绝，不再跳过垃圾或截断。纯候选失败保留映射与回读，
空覆盖恢复基础映射，reset 保留当前覆盖。有效样本逐位复制，每次非零
in 到达显式交付 1 号 out；零量子不交付，不支持输入输出重叠。

delay_line 的 in 每次实际到达推进一次环形历史，再显式交付 1 号 out，
不按 Task 去重；零量子不推进也不交付，下游失败不回滚已处理历史。
保留公共整数延迟、逐通道浮点延迟（加 0.5 后截断）与整数向量模式；
延迟 0 先写后读，切换延迟不重置历史，reset 清历史但保留控制值。
两个 BULK 槽按实例 channels 注册，保持双缓冲及实例 max_delay_samples 上限，
物理容量仍为 64 通道。浮点初值允许短 CSV 并补零；
非空整数初值必须恰好 channels 项，空整数初值保留配置数组。
初值、实际 BULK 延迟的非法/非有限/越界值明确拒绝，不再静默夹值或跳过垃圾。
纯候选校验失败保留旧参数和历史；缺失/短载荷及输入、输出、历史存储重叠拒绝。

bulk_relay 仍是控制数组资源，而非音频/BULK流转发器。
数值数组 values 保持 2048 项物理存储，逻辑 shape 为 param:count，
外部槽键由 bulk.slot_key 派生为 values_storage，控制参数与数据目录使用同一映射。
对外槽和双缓冲提交范围现在严格为逻辑 count，不再将未使用尾部暴露为有效数据。
初始完整物理数组默认全零；非空 initial_values 覆盖该初值，
允许不足 count 的逗号/空白分隔数值并补零，超过 count 或非法/非有限数值拒绝。
validate 与 prepare 共用解析器，纯校验失败保留旧参数和整个数组；
reset 保留数组。in/out 只有触发语义，每次实际到达同步交付一次，
包括零帧事件；不分配、不做同拍去重，下游失败不改写或回滚控制数组。

trigger_divider 是纯触发组件：每次实际收到 in 才推进一次计数，
仅在计数相位匹配 phase 时交付 out，随后按 factor 回绕。
不借助连线 every/phase 或调度器补调；载荷为空，时间与 epoch 原样交付。
factor/phase 均重建生效，必须满足 0<=phase<factor；reset 从相位0重新计数，
下游失败不回滚已消费的触发。factor 可配置，未硬编码一个可能失真的
drive_divide 绘图倍率。Task顺序示例使用本组件实现二分频。

wav_out 的 in 是处理入口，但它是终点，没有驱动出口，不隐式续调。
prepare 为会话分配原有的 16M 个 float 缓存，process 只验证并复制，
不分配、不做文件 IO；destroy 写出 16bit PCM 后释放缓存。
保持 [-1,1] 限幅、乘 32767 后向零截断，Inf 按限幅处理；
NaN 在缓存写入前拒绝，避免未定义的浮点到整数转换。
reset 仅清计数，下一段重新收集；空 file_path 不写文件。
单层父目录按原行为创建，路径超过 511 UTF-8 字节、输入不足、
容量不足或 WAV 字节率不能用 uint32 表示时明确失败。
溢出不再截断为成功；fopen/fwrite/fclose 错误返回失败，
destroy 同时写 stderr 生命周期诊断，避免宿主忽略返回值时静默丢失文件。
sample_rate=0 回读配置值 0，文件头使用输入配置的有效采样率；
没有输入采样率时保持 48000 fallback，正值按声明并接受图签名校验。

独立接口校验仅允许 sample_rate 引用显式 range 最小值为 0 的
int 签名参数，以 0 表示跟随；采样率接口间引用可传递这一语义。
channels、block_size、frame_size、shape 等尺寸仍须为正数，
包括间接引用可为 0 的 sample_rate；不会将“自动采样率”变成零长度载荷。

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

采样率支持 `in:sample_rate/param:factor`、
`in:sample_rate*param:up/param:down` 或从 `task:sample_rate` 开始的乘除链。
乘除项只能是正整数常量或影响签名的正整数参数，除数不得声明零下界；
这只是采样率签名，不推断调用频率，`drive_divide` 也仍仅供绘图。
独立CLI与公开Schema共同校验，图编译使用有理数保持精确比例。

Task顺序链中，已迁移的非源组件以实际Delivery提供的帧位置、epoch和标志为准，
plan标记 `timeline_from_delivery`，不再由Task静态节拍推断frame_origin/frame_step。
因此局部速率对应每Task拍的非整数帧数、大于Task量子的载荷均可合法交付；
不补静音，不增加等待。Runtime和生成代码在缺少Delivery时明确报错，
不得用节点调用计数伪造时间。源仍从自身Task/外部触发取得时间根。

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