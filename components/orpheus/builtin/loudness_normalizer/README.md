# orpheus.builtin.loudness_normalizer - 节目响度均衡器

面向视频、播客和普通节目播放的 RMS 自动电平控制：小声内容缓慢抬升，大声内容快速压低，使不同节目听起来更接近同一水平。

它不是 ITU-R BS.1770/EBU R128 LUFS 计量器：没有 K-weighting、门限积分或节目级扫描。它是低延迟、固定内存的实时节目电平器，适合电脑播放和嵌入式监听链路。

## 算法

1. 所有通道联合计算块 RMS，保持左右声像不变；
2. attack/release 平滑能量，得到 `input_db`；
3. 计算 `target_db - input_db`，限制在 `min_gain_db..max_gain_db`；
4. 大声时按 `gain_attack_ms` 快速压低，小声时按 `gain_release_ms` 缓慢抬升；
5. 输入低于 `gate_db` 时保持当前增益，不继续放大底噪；
6. 块内线性插值增益，避免块边界咔哒声。

该组件不做峰值前瞻，后面应连接 `limiter`。

## 推荐起点

| 参数 | 推荐值 | 说明 |
|---|---:|---|
| `target_db` | -20 dBFS | 普通视频/播客的保守 RMS 目标 |
| `min_gain_db` | -12 dB | 最多压低 12 dB |
| `max_gain_db` | +12 dB | 最多抬升 12 dB，避免底噪过度放大 |
| `gate_db` | -55 dBFS | 静音和极低底噪不参与追踪 |
| `detector_attack_ms` | 80 ms | 感知突然变大的内容 |
| `detector_release_ms` | 600 ms | 避免短暂停顿造成电平跳动 |
| `gain_attack_ms` | 80 ms | 快速降低大声内容 |
| `gain_release_ms` | 1200 ms | 缓慢提高小声内容，减少抽吸感 |

## 探针

- `input_db`：平滑后的输入 RMS；
- `gain_db`：当前自动增益；
- `output_db`：`input_db + gain_db` 的估算输出 RMS。

## 实时约束

`process` 内无动态分配、锁、日志或 IO；状态固定，最多 32 通道。所有通道共享一个增益，避免立体声像随内容漂移。
