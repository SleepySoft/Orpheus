# 噪声/失真检测(双端对照) 组件说明
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
  - `smoothing`(默认16)：平滑块数，越大越稳但反应越慢。
  - `threshold_db`(默认-40dB)：视为噪声帧的 THD+N 阈值。
  - `time_thres`(默认0.02)：时域残差峰值阈值。
- readback：`thd_n_db` / `noise_frames` / `noise_ratio` / `clicks` / `residue_pk` / `detail`(含低/中/高频分带相干)。

## 3. 用法（怎么接）
1. `ref` 接“处理前”的干净信号，`in` 接“处理后”的被测信号（两者需同步、同采样率）。
2. `out` 接后级（直通，可不接）。
3. 看 readback：
   - `thd_n_db` 尽量低，越接近-∞ 越干净；上升表示被测路引入了失真/噪声。
   - `noise_ratio` 超阈值帧占比，UI 节点依阈值变色。

## 4. 实例（对应 `test_noise_detectors.py` 双端用法）
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
