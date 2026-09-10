# 视频响度均衡：原子分解学习笔记

这个工程保留 `video_loudness_voice.yaml` 的实际使用链路，但把一体化 `loudness_normalizer` 拆成三个可观察原子。

## 信号与控制流

```text
音频：capture -> rms_detector -> gain_controller -> auto_gain -> 后续处理
控制：rms_detector.rms -> gain_controller.level
      gain_controller.gain_db -> auto_gain.gain_db
```

### 第 1 步：测量

`probe_rms` 对当前块的所有通道和样本计算：

```text
rms = sqrt(sum(sample^2) / sample_count)
```

这是线性幅值。`0.1` 对应 `-20 dBFS`，不能直接写进 dB 增益参数。

### 第 2 步：决策

`loudness_gain_control` 先把 RMS 平方恢复为能量并做 attack/release 平滑，再转换为 dB：

```text
input_db = 10 * log10(smoothed_energy)
desired_gain_db = clamp(target_db - input_db, min_gain_db, max_gain_db)
```

门限以下保持增益。大声内容使用较快的 `gain_attack_ms` 压低，小声内容使用较慢的 `gain_release_ms` 抬升，避免呼吸和抽吸感。

### 第 3 步：执行

`gain` 只负责把 dB 增益乘到样本上。这里将 `smoothing_ms` 设为 0，因为控制器已经完成快压慢抬；再次平滑会改变设定的时间常数。

## 为什么有两块延迟

Orpheus 控制链在每个图块结束时先读取全部源，再写入全部目标。每条控制连接固定一块延迟：

1. 第 N 块结束：RMS 写入控制器；
2. 第 N+1 块：控制器计算增益，块末写入 gain；
3. 第 N+2 块：gain 开始施加该增益。

48 kHz、128 帧时，两块约为 5.33 ms。稳态结果与一体化组件一致，一体化组件的瞬态响应则更紧凑。

## 建议观察

打开“控制链路”显示开关，可以看到两条橙色虚线。观察：

- `rms_detector.rms`：原始块 RMS；
- `gain_controller.input_db`：平滑节目电平；
- `gain_controller.gain_db`：当前自动增益；
- `gain_controller.output_db`：估算的归一化输出电平；
- `output_rms` / `output_peak`：后级人声 EQ 和 limiter 后的实际输出。

把一体化与分解示例使用相同参数试听，可以比较控制延迟对突发大声内容的影响。
