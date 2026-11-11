# 双端噪声与失真估计 组件说明
> 组件 ID `orpheus.builtin.noise_detector_ab` · 类型：纯观测（直通输出，不处理）
> 需要一个“干净参考 `ref`”和一个“被测信号 `in`”，用互功率谱 + 相干检测“参考 → 被测”路径引入的失真与噪声。

## 1. 原理：互功率谱与相干
把 `ref` 与 `in` 同步分帧 FFT，逐 bin 维护：
- 自功率谱 `Sxx`(ref) / `Syy`(in) 与互功率谱 `Sxy`。
- 相干 `γ² = |Sxy|² / (Sxx·Syy)`，衡量“线性路径能解释的部分”。
- 总能量里 `(1-γ²)` 的部分，就是与 `ref` 非线性相关的残余：非源同步噪声 + 非线性失真。
- 汇总为 THD+N(dB)、超过 `threshold_db` 的帧数/占比、时域残差峰值与突刺计数。

## 2. 端口与参数
- 端口：`ref`(干净参考) / `in`(被测) / `out`(直通输出)。
- 参数：
  - `channels`(默认2)：通道数，restart生效。
  - `fft_size`(默认128)：4～4096的2次幂，不由Task块长推断。
  - `batches`(默认1)：完整顺序分析窗口数，1～64；载荷为 `fft_size*batches`。
  - `smoothing`(默认16)：平滑窗口数，越大越稳但反应越慢。
  - `threshold_db`(默认-40dB)：视为噪声帧的 THD+N 阈值。
  - `time_thres`(默认0.02)：时域残差峰值阈值。
- readback：`thd_n_db` / `noise_frames` / `noise_ratio` / `clicks` / `residue_pk` / `detail`(含低/中/高频分带相干)。

## 链式调用与统计契约

- `ref` 为数据输入，不驱动；`in` 是唯一驱动入口，直接ABI调用也拒绝以ref入口触发。
  上游须先准备ref，再向in交付，两输入都须绑定完整显式载荷。不增加会合等待。
- 保留原Hann/f32 FFT、每窗谱EMA与复互谱符号；参考/被测FFT使用prepare分配的工作区，
  不再在实时调用栈上放两组4096点数组。相干计算排除DC/Nyquist，并按原低/中/高频边界汇总。
- `thd_n_db` 仍为本次全部窗口平滑功率加权的残差比，最低-120dB；
  每个窗口分别按 `THD+N > threshold_db` 计入噪声窗口。
  `noise_ratio = 噪声窗口数/实际总窗口数`，不再“每次调用最多计1、分母却加窗口数”。
  单窗口行为不变；多窗口行为有意修正。
- `time_thres` 统计 `abs(ref-in)>阈值` 的样本；clicks沿用
  `abs(ref-in)>0.2` 的残差样本计数，**不是**单端检测的相邻样本差分。
  `residue_pk` 为reset以来的最大残差，不是仅本次峰值。
- 统计提交后才显式 `emit out(1)`，同拍多次交付分别处理、下游失败不回滚。
  输出不接时仍更新观测，不伪造交付；输入可共享只读载荷，输出原地/部分重叠/私有状态别名拒绝。
- 计数均使用64位；总窗口/样本溢出和计数不变量错误在推进前明确拒绝。
  noise_frames/clicks的int32探针投影饱和到INT32_MAX，JSON保留完整64位计数。
  JSON中的total_frames/noise_frames均表示窗口数。
- JSON只在非实时getter中由最新指标生成；名称、ID与生成探针枚举一致。
  非有限值、容量与格式化错误明确报错，不静默截断。零帧不推进，
  reset清谱、计数、残差峰值和全部探针，保留配置。

## 3. 用法（怎么接）
1. `ref` 接“处理前”的干净信号，`in` 接“处理后”的被测信号（两者需同步、同采样率）。
2. `out` 接后级（直通，可不接）。
3. 看 readback：
   - `thd_n_db` 尽量低，越接近-∞ 越干净；上升表示被测路引入了失真/噪声。
   - `noise_ratio` 超阈值帧占比，UI 节点依阈值变色。

## 4. 原工程示意

旧 `test_noise_detectors.py` 双端工程仍待整体迁移，不能依赖独立源碰巧先填ref；
迁移时须显式声明一致FFT载荷并确认真实来源顺序。以下仅示意端口角色。
```yaml
graph:
  nodes:
    - id: wr  ; component: orpheus.builtin.wav_in ; params: {file_path: ref.wav, channels: 1}
    - id: wi  ; component: orpheus.builtin.wav_in ; params: {file_path: in.wav, channels: 1}
    - id: ab  ; component: orpheus.builtin.noise_detector_ab ; params: {channels: 1, smoothing: 16, threshold_db: -40, time_thres: 0.02}
    - id: out ; component: orpheus.builtin.wav_out ; params: {file_path: outputs/out.wav, channels: 1, sample_rate: 48000}
  connections:
    - {from: wr:out, to: ab:ref}
    - {from: wi:out, to: ab:in}
    - {from: ab:out, to: out:in}
```
验证：`in == ref` → `thd_n_db < -40`、`noise_ratio ~ 0`；在被测路加一路相干白噪 → `thd_n_db` 上升、`noise_ratio` 显著增大。详见 `docs/HOW.md` §30。

## 定向验证

- `orpheus_core/tests/test_noise_detector_ab_interfaces.py`：5图×四入口，
  解析同位冲激验证直通、谱/时域统计、32通道/4096点/64批次边界、
  阈值等号、ref数据口不驱动、输出不连接时仍观测，以及名称/ID/枚举JSON完整结构。
- `tests/noise_detector_ab_smoke.c`：独立double DFT验证复互谱误差
  `<1e-5*(1+abs(reference))`、分带相干误差<1e-5、THD误差<0.002dB，
  并验证逐窗口计数、残差峰值、七次分配失败清理重试、下游错误后继续、
  实时无格式化、非法入口/输入/容量/别名/配置、计数溢出/饱和、
  纯观测、零帧、reset和非有限JSON拒绝。
