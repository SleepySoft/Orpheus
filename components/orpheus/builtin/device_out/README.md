# orpheus.builtin.device_out — 设备输出

## 功能

把图处理后的音频送到声卡播放。与 `device_in` 一样，**组件 C 代码是占位**：`process` 什么都不做，实时宿主 `rt_host` 在图调度**之后**把本组件的输入 buffer 拷贝给播放设备。`device` / `sample_rate` 是 yaml-only 参数，由 rt_host 消费。

图里只有 `device_out`（没有 `device_in`）时，播放设备本身就是整条链路的时钟（如 `wav_in` → `device_out` 的边解码边听场景）。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | 待播放的音频（interleaved f32），`channels` 通道。未连接时设备输出静音 |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `device` | string | `""` | 播放设备名，留空 = 系统默认设备。按设备名**子串、不区分大小写**匹配；匹配不到则启动报错。restart_required |
| `channels` | int | 2 | 播放通道数，1~32；与 `device_in` 的通道数**解耦**（采集 8 通道、播放 2 通道完全合法）。affects_signature，restart_required |
| `sample_rate` | int | 0 | 期望的播放采样率（Hz），0 = 继承工程全局采样率，0~192000。affects_signature，restart_required |

### `sample_rate` 的实际作用

本组件**不是**时钟源（manifest 无 `clock_source`），图的采样率由工程全局或时钟源节点（`device_in` / `wav_in` 等）决定；实际打开设备时 rt_host 对采集/播放两侧使用同一个图采样率。这里的 `sample_rate` 主要用于在工程层面声明意图与参与端口签名校验。启动时宿主会按最终采样率校验设备能力：

- `native`：设备原生支持；
- `WARN ... will convert`：由 miniaudio 自动转换（有额外延迟/音质代价）；
- 完全不支持则设备初始化失败、运行退出。

## 注意事项

- **平台限制**：`platforms: [win]`，仅 Windows 实时路径可用；代码生成（嵌入式）路径请用 `embed_out`。
- 设备拓扑同 `device_in` 的说明：双默认设备走 duplex（最低延迟）；指定设备或 loopback 组合走异步桥（环形缓冲解耦两台设备的时钟）。
- 设备周期 ≥10ms 且与图块长解耦，宿主负责切块调度。
- 全部参数 restart_required，运行时修改无效。

## 典型用法

```yaml
component: orpheus.builtin.device_out
params:
  device: ""            # 默认播放设备
  channels: 2
  sample_rate: 0
```

```
device_in ──► 处理链 ──► device_out        # 实时监听
wav_in ──► gain ──► device_out             # 文件试听到声卡
```

## 实时安全

manifest 标记 `realtime_safe: true`：`process` 无分配、无锁、无 IO（它甚至不读输入数据，搬运由宿主完成）。
