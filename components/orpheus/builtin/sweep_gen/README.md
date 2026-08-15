# orpheus.builtin.sweep_gen — 扫频发生器

## 功能

测试信号源：生成一个频率随时间从 `start_freq` 扫到 `end_freq` 的正弦波（chirp），扫满 `duration_s` 秒后输出静音。它是 `clock_source`（synthetic 时钟域），自身即图的时钟根，`sample_rate` 参数决定整张图的采样率。主要用于配合 `sweep_record` 做频率响应测量，也可做听音测试、滤波器试听扫频。

## 参数

| 参数 | 类型 | 默认值 | 范围 | 更新策略 | 说明 |
|---|---|---|---|---|---|
| `sample_rate` | int | 48000 | 8000–192000 | restart_required | 采样率（Hz），时钟源声明，成为图采样率。 |
| `start_freq` | float | 20.0 | 1.0–20000.0 | restart_required | 起始频率（Hz）。 |
| `end_freq` | float | 20000.0 | 1.0–20000.0 | restart_required | 结束频率（Hz）。 |
| `duration_s` | float | 5.0 | 0.1–300.0 | restart_required | 扫频时长（s）。 |
| `amplitude` | float | 0.7 | 0.0–1.0 | smoothed | 输出幅度，运行时可调。 |
| `log_scale` | bool | true | — | restart_required | true=对数扫频，false=线性扫频。 |
| `channels` | int | 2 | 1–32 | restart_required | 通道数（所有通道输出同一样本），影响端口签名。 |

### 对数（log）与线性（linear）扫频

频率随时间的轨迹：

- **log_scale = true（对数）**：`f(t) = f0 × (f1/f0)^(t/dur)`。每倍频程花同样时间，低频段停留久、高频段过得快。听感上音高均匀上升，是频响测量的标准选择——低频分辨率天然更高。
- **log_scale = false（线性）**：`f(t) = f0 + (f1−f0) × (t/dur)`。每秒扫过的赫兹数恒定，高频段占大部分时间。适合需要在某一线性频段内均匀采样的场合。

无论哪种模式，相位都是连续累积的（`phase += 2πf·dt`），所以波形全程无跳变、无咔哒声。

### 扫完之后

`t ≥ duration_s` 后输出静音（0），`current_freq` 探针归零，`progress` 停在 1.0。**离线运行（文件宿主）时长正是由 `duration_s` 决定的**：编译器取图中 `sweep_gen` 的 `duration_s` 作为无文件输入时的运行时长，所以设 5 秒就跑 5 秒——想跑满整条扫频，不要把 `duration_s` 设短了又指望后面有输出。

### 探针

| 探针 | 类型 | 说明 |
|---|---|---|
| `progress` | float | 扫频进度 0~1，扫满后保持 1.0。 |
| `current_freq` | float | 本块最后一个样本的瞬时频率（Hz）；扫完为 0。 |

## 端口

| 端口 | 方向 | 说明 |
|---|---|---|
| `out` | 输出 | 扫频正弦，`channels` 通道 × `sample_rate` 采样率。 |

## UI 行为

UI 节点本体（`nodeWidgets.js` 的 `SweepGenWidget`）显示一条进度条和当前频率（如 `1.25 kHz`），扫完显示"完成/已结束"，方便确认此刻是谁在发声。

## 注意事项

- 除 `amplitude` 外所有参数都是 `restart_required`：改起止频率、时长、对数开关、通道数都需要重新编译/重启。
- `end_freq` 可以小于 `start_freq`（向下扫），公式同样成立。
- `amplitude` 为 smoothed，运行时调整不会爆音；上限 1.0，测量时建议留余量（默认 0.7）。
- 实时安全：`process` 内仅 `sin`/浮点累加，无分配、无 IO。

## 典型用法

频响测量（发生器 + 记录器成对使用，见 `sweep_record` 的 README）：

```
sweep_gen ──► 被测链路 ──► sweep_record ──► wav_out
```

听音扫频：直接接到 `main_out`，用耳朵确认音箱/房间在哪个频率有问题。

## 实时安全

`process` 无内存分配、无阻塞、无 IO；三角函数调用为逐样本一次 `sin`，实时安全。
