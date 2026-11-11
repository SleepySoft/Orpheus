# 单端噪声特征检测 组件说明
> 组件 ID `orpheus.builtin.noise_detector` · 类型：纯观测（直通输出，不做任何去噪/滤波）
> 靠 readback 上报指标并依阈值给 UI 节点加黄/红预警色。

## 1. 为什么需要它
“主观视听”难以定量“这一段到底噪不噪、哪里爆音”。本组件在流程中插一个“观测探针”节点，把信号“噪声化”的几个维度数字化：

- 频谱平坦度（噪声为主 vs 乐音集中）
- 宽带噪声底（相对信号的底噪高低）
- 时域突刺计数 / 削波占比（爆声/杂音/过载）

它不做自动处理——“怎么处理”由你在流程中另接其他组件决定。

## 2. 端口与参数
- 端口：`in`（输入） / `out`（直通输出），通道数取决于 `channels`。
- 参数：
  - `channels`(默认2)：通道数，restart生效。
  - `fft_size`(默认128)：4～4096的2次幂，不从Task块长推断。
  - `batches`(默认1)：一次载荷内的完整顺序窗口数，1～64。
  - `clip_level`(默认0.999)：绝对值大于等于此阈值计为削波。
  - `click_thres`(默认0.3)：同一通道相邻样本差分严格超过此阈值计为突刺。
- readback：`flatness` / `noise_floor_db` / `clicks` / `clip_pct` / `detail`(JSON)。
  - `flatness` → 0 代表纯音集中，→ 1 代表白噪全频铺平。
  - `noise_floor_db`：底噪相对信号的 dB。

## 链式调用与数值边界

- `in` 是唯一驱动入口，输入/输出均显式声明载荷 `fft_size*batches`，
  不丢弃尾部、不为不足窗口伪造平坦度、不调整Task时间锚点或有效帧元数据。
- 保留原f32 Hann窗、基2FFT、排除DC/Nyquist的功率平坦度，
  每窗各频点加1e-12后合并求几何/算术均值比，再跨通道平均。
  平坦度沿用原double EMA（0.95/0.05，旧值为0时直接采用当前值）。
- 噪声底保留原分支与能量分母 `样本数+1`；削波比是0～1的累计比例，并非0～100百分数。
  突刺只比较本次载荷内同通道的相邻样本，不把相邻声道或两次交付边界当作突刺。
- 统计提交后显式 `emit out(1)`；同拍重复交付分别执行，下游失败不回滚。
  零帧不推进或交付，原地/部分重叠/私有状态别名均拒绝。
- 内部样本/削波/突刺采用64位计数，溢出在状态推进前明确拒绝。
  `clicks` 的int32探针投影饱和到INT32_MAX，不再因槽直读变成负数；
  JSON中的clicks保留完整64位计数。
- JSON只在非实时getter中由最新指标生成，名称、ID及生成探针枚举一致。
  非有限数值、容量与格式化错误明确报错，不输出无效JSON；reset清统计并保留窗配置。

## 3. 用法（怎么接）
1. 在被测节点（EQ / 软限幅 / 调色 / 混音等）的“输出之后”插入本组件：`前级:out → in`，`out → 后级:in`。
2. `in` 接好后运行，看 指标 readback：
   - `flatness` 接近1 且 `noise_floor_db` 高 → 噪声为主；反之纯音集中。
   - `clicks` / `clip_pct` 非零 → 爆声/杂音/过载，提示查前级是否过载。
3. 想逐级定位：把本组件插入流程各个节点后，看哪个级后 `flatness`/底噪突变。

## 4. 原工程示意

旧 `test_noise_detectors.py` 及普通工程仍待整体迁移，接线需显式指定一致的载荷。
以下示意省略FFT规格，不作为已迁移工程的验收依据。
```yaml
graph:
  nodes:
    - id: wav_in ; component: orpheus.builtin.wav_in ; params: {file_path: input.wav, channels: 2}
    - id: nd      ; component: orpheus.builtin.noise_detector ; params: {channels: 2, clip_level: 0.999, click_thres: 0.3}
    - id: out     ; component: orpheus.builtin.wav_out ; params: {file_path: outputs/out.wav, channels: 2, sample_rate: 48000}
  connections:
    - {from: wav_in:out, to: nd:in}
    - {from: nd:out, to: out:in}
```
快速验证：输入一段干净正弦 → `flatness` 很低；换成白噪 → `flatness` 接近1、`noise_floor_db` 提高。相关组件共同读取见 `docs/HOW.md` §30。

## 定向验证

- `orpheus_core/tests/test_noise_detector_interfaces.py`：4图×四入口，
  独立冲激频谱/时域统计验证逐位直通、32通道/4096点/64批次边界、
  阈值等号、真实累计计数及名称/ID/枚举JSON读回。
- `tests/noise_detector_smoke.c`：独立double DFT验证Hann平坦度（误差<1e-5）、
  噪声底两分支（误差<1e-4）、安静/声道常量/复杂频谱/冲激，
  并验证无跨声道误报、削波比逐位、三次分配失败重试、下游错误后继续、
  无实时格式化、非法输入/别名/配置、计数溢出与投影饱和、零帧、reset及非有限JSON拒绝。
