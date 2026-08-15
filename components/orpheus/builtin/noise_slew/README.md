# orpheus.builtin.noise_slew — 变化率限幅

## 功能

逐样本**变化率限幅（slew-rate limiting）**：输出样本相对上一个样本的增量被钳制在 ±delta 以内，上升用 `rise_rate`、下降用 `fall_rate`。突变（爆音、咔哒、野值尖刺）被"削斜"成斜坡，缓变信号原样通过。

工作原理（`src/noise_slew.c`）：

```
delta = x[n] - y[n-1]
maxd  = delta > 0 ? rise_delta : fall_delta      （每样本最大增量 = rate / sample_rate）
delta = clamp(delta, -maxd, +maxd)
y[n]  = y[n-1] + delta
```

即一个非线性斜坡跟随器：信号变化比限额慢时无失真跟踪；变化比限额快时按最大斜率追赶。每通道独立记忆上一个输出样本（`prev[c]`）。

### 用途：防爆音/抑制突变噪声

- **防爆音**：链路切换、参数跳变、外部数据丢包造成的电平阶跃会被拉成斜坡，消除"咔哒"声。
- **去野值**：传感器/控制信号里的单样本尖刺被限斜率削平。
- **包络整形**：rise/fall 不对称（如快上升慢下降）可当简易包络器用。

代价：限幅本身是一种低通效果——上升沿被拉斜会损失高频瞬态，`rate` 设太低会让正常音频变"闷"。

## 端口

| id | 方向 | 说明 |
|---|---|---|
| `in` | input | 音频输入，`channels` 通道 |
| `out` | output | 限斜率后的音频，`channels` 通道（支持原地处理 `supports_inplace`） |

## 参数

| 参数 | 类型 | 默认值 | 范围 | update_policy | 说明 |
|---|---|---|---|---|---|
| `rise_rate` | float | 1.0 | 0.0001~100.0 `/s` | smoothed | 上升速率：幅度每秒最多增加多少 |
| `fall_rate` | float | 1.0 | 0.0001~100.0 `/s` | smoothed | 下降速率：幅度每秒最多减少多少 |
| `channels` | int | 2 | 1~32 | restart_required（影响签名） | 通道数 |

### 关键参数详解

- **单位 `/s` 的含义**：满幅归一化信号下，`rate = 1.0` 表示输出从 -1 爬到 +1 至少需要 2 秒。换算到每样本：`delta_max = rate / sample_rate`（prepare 时预计算）。48 kHz 下 `rate = 1.0` → 每样本最多动 ≈ 2.1e-5。
- **rise/fall 不对称**：`rise_rate > fall_rate` 快起慢落（类似峰值保持包络）；反向则慢起快落。
- `smoothed` 策略意味着运行时可调且不需要重启；代码内 `set_parameter` 直接写入新速率（下一拍生效，对限额参数的阶跃本身无害——它只影响后续斜率上限）。

## 注意事项

- `sample_rate_independent: false`：每样本限额在 `prepare` 按采样率换算，换采样率需重新 prepare/编译。
- `prev[c]` 是固定 32 通道数组，`channels` 请勿超出 manifest 范围 [1, 32]。
- `reset` 把各通道 `prev` 清零：重启后第一个样本会从 0 开始爬坡，若输入首帧电平高，开头会有一段受限于 rise_rate 的爬升。
- 这是逐样本状态滤波器，块边界无缝（状态保留在组件内）。

## 典型用法

```
不干净的控制/切换信号 ──► noise_slew(rise_rate=2, fall_rate=0.5) ──► 增益/开关控制输入
音频链末端 ──► noise_slew(rise_rate=10, fall_rate=10) ──► device_out   （防爆音兜底）
```

## 实时安全

process 内仅逐样本加减与比较，无堆分配、无锁、无 IO、无除法（限额已预计算）；`realtime_safe: true`，且支持原地处理。
