# orpheus.builtin.loudness_gain_control - RMS自动增益控制

这是 `loudness_normalizer` 内部控制算法的教学原子：输入线性 RMS，输出应施加的 dB 增益。它不测量音频，也不施加增益；音频端口只做逐样本直通，让控制器与检测器、增益器处在同一 Task 和时间线上。

接口声明是事实源，独立生成入口：

```powershell
python scripts\generate_component.py components\orpheus\builtin\loudness_gain_control\component.yaml
```

每次音频`in`真实到达独立推进检测/增益状态，然后自主交付1号`out`；
不按Task去重，不增加额外平滑或隐藏调用。音频逐位直通，包含NaN/Inf/负零；
精确原地合法，部分重叠、与实例状态重叠及不足载荷拒绝。
下游错误不回滚已更新的控制状态。

## 分解连接

```text
probe_rms --audio--> loudness_gain_control --audio passthrough--> gain
    rms --control--> level                   gain_db --control--> gain_db
```

`gain.smoothing_ms` 应设为 0，因为快压慢抬已经由本组件完成。Orpheus 控制链在块末采用两相快照，每条链固定一块延迟，因此从 RMS 测量到增益实际生效共两块；48 kHz、128 帧时约为 5.33 ms。

## 计算过程

设 `level` 是线性 RMS：

```text
raw_energy = level^2
smoothed_energy += detector_coeff * (raw_energy - smoothed_energy)
input_db = 10 * log10(smoothed_energy)
desired_gain_db = clamp(target_db - input_db, min_gain_db, max_gain_db)
gain_db += gain_coeff * (desired_gain_db - gain_db)
output_db = input_db + gain_db
```

块级平滑系数为：

```text
coefficient = 1 - exp(-frames / (time_ms * 0.001 * sample_rate))
```

- 原始输入高于平滑检测值时使用 `detector_attack_ms`，反之使用 `detector_release_ms`；
- 目标增益低于当前增益时使用较快的 `gain_attack_ms`，表示快速压低大声内容；
- 目标增益高于当前增益时使用较慢的 `gain_release_ms`，表示缓慢抬升小声内容；
- `level` 低于 `gate_db` 时保持当前增益，避免在静音段继续放大底噪。
- 保留冷启动首次有效能量直接建立包络、`1e-12`能量下限和逐项f32运算次序。
  零帧仍按原算法使用系数1推进控制状态，并交付零帧音频；不统一添加早退。
  reset清检测历史及三个探针，保留所有配置。
- `input_db`、`gain_db`、`output_db`明确只读；参数通过同一声明约束类型和范围，
  不再从手写描述符、注册表和参数解析各维护一份元数据。

## 参数与探针

| 参数 | 作用 |
|---|---|
| `level` | 线性 RMS 控制输入，通常连接 `probe_rms.rms` |
| `target_db` | 期望节目 RMS 电平 |
| `min_gain_db` | 最大衰减下限 |
| `max_gain_db` | 最大提升上限 |
| `gate_db` | 静音门，门下保持增益 |
| `detector_attack_ms/release_ms` | RMS 能量包络速度 |
| `gain_attack_ms/release_ms` | 自动增益下降/上升速度 |
| `input_db` | 平滑后的输入电平 |
| `gain_db` | 当前目标增益，连接到 `gain.gain_db` |
| `output_db` | 估算输出电平 |

## 与一体化组件的区别

- 稳态数学与 `loudness_normalizer` 相同；
- 分解版多两块控制延迟，便于观察每个阶段；
- 一体化版在同一组件内测量并施加增益，响应更紧凑，生产使用更方便；
- 两者都不是标准 LUFS 计量器，峰值保护仍应由后级 `limiter` 完成。
