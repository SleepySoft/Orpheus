# orpheus.builtin.device_in — 音频采集

## 功能

从声卡采集音频（麦克风/线路输入，或系统声音环回）并注入图。它是 `clock_source`（`clock_domain: device`）：整条实时链路以设备回调为节拍推进。

**重要：这个组件的 C 代码是个"占位"**。`process` 什么都不做——真正打开设备、采集、填数据的是实时宿主 `rt_host`（基于 miniaudio）：宿主在每次图调度**之前**，把刚采集到的一块样本直接写进本组件的输出 buffer。所以你看到的 `device` / `source` / `sample_rate` 参数只存在于工程 YAML 里（C ABI 描述符只有 `channels`），由 rt_host 从 plan 读取并消费。这类"yaml-only 参数"改的是宿主行为，不是组件行为。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| out | output | audio | 采集到的音频（interleaved f32），`channels` 通道 |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `device` | string | `""` | 采集设备名，留空 = 系统默认设备。按设备名**子串、不区分大小写**匹配；匹配不到则启动报错。restart_required |
| `source` | string | `microphone` | 采集源：`microphone` = 麦克风/线路输入；`loopback` = 系统声音（环回采集）。restart_required |
| `channels` | int | 2 | 采集通道数，1~32。affects_signature，restart_required |
| `sample_rate` | int | 0 | 设备请求采样率（Hz），0 = 继承工程全局采样率，0~192000。affects_signature，restart_required |

### `source = loopback` 是什么

Loopback（环回）采集的是"声卡正在播放的声音"而不是麦克风——比如录下播放器、浏览器的输出。选择 loopback 后，`device` 的含义反转为"要监听哪台**播放**设备"（默认 = 默认扬声器）。常用于：系统声音分析、把播放器输出灌进处理链再录回。

### `sample_rate`：0 = 继承，非 0 = 源驱动

- `0`：跟随工程全局采样率，最省心的选择。
- 非 0：作为时钟源参数**驱动整张图的编译期采样率**（多个时钟源必须一致）。
- 启动时 rt_host 会校验设备能力并在日志中报告三档结果：
  - `native`：设备原生支持该通道数/采样率；
  - `WARN ... will convert`：设备不原生支持，由 miniaudio 自动做格式/采样率转换（能用，但有额外延迟与音质代价）；
  - 彻底不支持的组合会在设备初始化时报错退出。

### 与 device_out 的通道解耦

采集与播放的通道数**互不影响**：`device_in` 的 `channels` 决定输入侧（如 8 通道麦克风阵列），`device_out` 的 `channels` 决定输出侧（如 2 通道耳机），两侧独立配置。

## 注意事项

- **平台限制**：`platforms: [win]`，仅 Windows 实时路径可用（loopback 依赖 WASAPI）。
- 设备拓扑由 rt_host 按图内容自动选择：
  - `device_in` + `device_out` 都用默认设备 → 单个 duplex 设备（同一时钟，延迟最低）；
  - 指定了具体设备、或 loopback 模式 → 异步桥：采集 → 环形缓冲（默认 100ms）→ 播放设备为主时钟，可容忍两台设备时钟漂移（如虚拟声卡 + 耳机）；
  - 只有 `device_in` → 采集/环回设备就是时钟（如系统声音 → wav_out 录制）。
- 设备回调周期（≥10ms）与图块长解耦：宿主会把较大的设备周期切成 `block_size` 小块逐块调度。
- 修改任何参数都要重启运行（全部 restart_required）。

## 典型用法

```yaml
component: orpheus.builtin.device_in
params:
  device: ""            # 默认采集设备
  source: microphone
  channels: 2
  sample_rate: 0        # 继承工程采样率
```

```
device_in ──► gain ──► device_out                # 实时直通监听
device_in(source=loopback) ──► wav_out           # 录系统声音到文件
```

## 实时安全

manifest 标记 `realtime_safe: true`：组件本身不做任何工作（数据由宿主预填），`process` 无分配、无锁、无 IO。
