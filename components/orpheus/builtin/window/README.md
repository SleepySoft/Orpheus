# orpheus.builtin.window — 窗函数

## 功能

按系数数组对输入逐样本加权：`out[f] = in[f] × w[f]`。两种模式：

- **single**：每块从头应用一次窗；块内超出 `window_size` 的部分直通（系数视为 1.0）。
- **repeat**：以 `window_size` 为周期循环应用窗——块内第 f 个样本用 `w[f % window_size]`。

### 为什么需要窗函数

对一段有限长信号直接做 FFT，相当于默认乘了矩形窗：帧边界的硬截断会在频域产生**频谱泄漏**（能量从真实频点扩散到邻近 bin，表现为 skirts / 旁瓣），严重时淹没弱信号。加窗（Hann、Hamming、Blackman…）让帧两端平滑归零，用主瓣变宽换旁瓣大幅压低，频谱分析才"看得准"。

本组件是 RNC TID5/TID6 STFT 分析链的一环：典型接法是 `circular_buffer`（分帧）→ **window**（repeat 模式，窗长=帧长）→ `rfft`（功率谱）→ `spectral_reduce`（聚合）。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | 输入音频，`channels` 通道 |
| out | output | audio | 加权后的音频，通道数与输入相同 |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `window_size` | int | 256 | 窗长，2~4096；restart_required |
| `coefficients` | string | "1.0" | 窗系数串（BULK 文本），逗号/空格分隔的浮点数；restart_required |
| `channels` | int | 2 | 通道数，1~32；改变后需重新编译（affects_signature） |
| `mode` | string | "single" | `single` 或 `repeat`；restart_required |

## 关键参数详解

### `coefficients`：自带系数，而非内置窗型

组件**不内置** Hann/Hamming 等窗型，系数完全由 `coefficients` 字符串给出（逗号/空格/制表符/换行分隔，最多解析 `window_size` 个）。这带来两点：

- 任意窗都能用：Hann、Blackman、Kaiser、平顶窗，甚至非标准 taper——由 Python/MATLAB 生成后粘贴即可。
- 解析为空时兜底为 `[1.0]`（即直通矩形窗）；**未提供的尾部系数为 0**（coeffs 数组先清零再填充），所以请确保系数串长度 ≥ window_size，否则窗尾会被乘成 0 而不是直通。

128 点 Hann 窗的例子（对称，`w[n] = 0.5·(1 − cos(2πn/(N−1)))`）见 `examples/symphony_asm_ehc_rnc.yaml` 中 `win` 节点。

### `mode`：single vs repeat

- **repeat** 是 STFT 场景的正确选择：`circular_buffer` 输出的块是 `num_frames` 个帧首尾相接（块长 = num_frames × frame_size），把 `window_size` 设为帧长，窗就会逐帧循环套用。
- **single** 适合"每块开头做一次加权、其余不动"的场景；注意它是**每块都从头**应用（不是只应用一次后就永久直通）。

## 注意事项

- 所有参数 restart_required：运行时不能换窗。
- 窗系数缓存在组件状态内的定长数组（`float coeffs[4096]`），`window_size` 上限 4096。
- `reset` 会把窗系数重置为直通（`[1.0]`）且 mode 归 single——不会重新解析参数字符串；工程上 reset 语义以 prepare 为准，注意不要依赖 reset 保留自定义窗。
- 同一组窗系数对所有通道生效（标量窗，无每通道窗）。

## 典型用法

```yaml
# STFT 链：分帧 → 加窗 → FFT → 聚合
- id: cb
  component: orpheus.builtin.circular_buffer
  params: { channels: 12, frame_size: 128, hop_size: 64, num_frames: 24 }
- id: win
  component: orpheus.builtin.window
  params:
    channels: 12
    window_size: 128
    mode: repeat
    coefficients: "0.000000, 0.000612, 0.002446, ..."   # 128 点 Hann
- id: fft
  component: orpheus.builtin.rfft
  params: { channels: 12, fft_size: 128, output_mode: power }
```

（完整链路见 `examples/symphony_asm_ehc_rnc.yaml` 的 tid5/tid6 任务。）

## 实时安全

- `process` 无内存分配、无锁、无 IO，纯查表乘法，支持就地处理（supports_inplace）。
- 系数解析（strtod）只在 prepare 进行。
