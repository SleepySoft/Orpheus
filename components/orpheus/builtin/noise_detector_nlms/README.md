# 噪声检测(NLMS残差) 组件说明
> 组件 ID `orpheus.builtin.noise_detector_nlms` · 类型：纯观测（直通输出，不处理）
> 双端 A/B：用 **NLMS 自适应滤波器**学习 `ref → in` 的线性冲击响应，把“线性可解释部分”剥离后剩下的残差就是噪声/失真。

## 1. 原理：NLMS 残差
用一个自适应 FIR `w` 去复现 `ref → in` 的线性传输，预测值 `y = wᵀ x`（`x` 为 `ref` 的延迟信号向量），残差：
\[ e = in - y = in - wᵀ x \]
这个 `e` 就是“线性路径无法解释的部分”：非线性失真 + 添加噪声 + 突发杂音。
更新规则（逐样本、逐通道独立）：
\[ w \leftarrow leak\cdot w + \mu\frac{e\,x}{\lVert x\rVert^2+\varepsilon} \]
“归一化”是指除以输入能量 `‖x‖²`，这样对信号幅度不敏感、收敛更稳定；`ε` 避免除零。

## 2. 端口与参数
- 端口：`ref`(干净参考) / `in`(被测) / `out`(直通输出)。
- 参数：
  - `channels`(默认2)：通道数。
  - `filter_length`(默认64)：自适应滤波器阶数。越大能复现越长的回声/延迟，但收敛更慢。
  - `step_size`(默认0.1)：步长，收敛速度 vs 稳定性（过大挥动/发散）。
  - `leakage`(默认1.0)：泄漏因子，低于1 提升稳定性。
  - `eps`(默认1e-6)：正则化。
  - `time_thres`(默认0.02)：时域残差峰值阈值。
  - `frame_thres_db`(默认-25dB)：帧残差预算阈值，超过则计为噪声帧。
- readback：`residue_db` / `erle_db` / `noise_frames` / `noise_ratio` / `clicks` / `residue_pk` / `detail`。
  - `residue_db`：残差/被测能量比(dB)，越小越干净。
  - `erle_db`：被测能量/残差能量比(dB)，越大越干净。

## 3. 用法（怎么接）
1. `ref` 接干净参考，`in` 接被测信号，`out` 直通输出（可不接）。
2. 调参：先用小 `step_size`、中等 `filter_length`，给足收敛时间再看 `residue_db`。
3. 看 readback：`residue_db` / `erle_db` 改善 → 被测路干净；引入相干噪声 / 非线性失真 → 残差上升。

## 4. 实例（对应 `test_noise_detectors.py` NLMS 用法）
```yaml
graph:
  nodes:
    - id: wr ; component: orpheus.builtin.wav_in ; params: {file_path: ref.wav, channels: 1}
    - id: wi ; component: orpheus.builtin.wav_in ; params: {file_path: in.wav, channels: 1}
    - id: nl ; component: orpheus.builtin.noise_detector_nlms ; params: {channels: 1, filter_length: 64, step_size: 0.1}
    - id: out; component: orpheus.builtin.wav_out ; params: {file_path: outputs/out.wav, channels: 1, sample_rate: 48000}
  connections:
    - {from: wr:out, to: nl:ref}
    - {from: wi:out, to: nl:in}
    - {from: nl:out, to: out:in}
```
验证：`in == ref` → `residue_db < -40`；加相干白噪 → 残差上升；非线性失真(幅值三次) 也会被 NLMS 捕获。用法对比见 `docs/HOW.md` §30。
