# orpheus.builtin.device_out — 声卡音频输出

## 功能

把图处理后的音频交给宿主播放。`in` 是唯一驱动入口（`process: true`），
组件校验本次真实交付后结束调用，不自动续调、不执行设备 IO 或额外复制。
动态 `rt_host` 与生成 `host_win` 在链返回后读取输入缓冲，交给 miniaudio。
唯一配置源为 [component.yaml](component.yaml)，独立脚本生成 ABI v7 壳，
`device/channels/sample_rate` 均进入 ABI 参数描述符，不再只是 yaml-only 参数。

图里只有 `device_out`（没有 `device_in`）时，播放设备本身就是整条链路的时钟（如 `wav_in` → `device_out` 的边解码边听场景）。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | 待播放的音频（interleaved f32），`channels` 通道；缺失输入明确报错 |

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

- **平台限制**：`platforms: [win]`，动态与生成 win 宿主均可使用；DSP 部署请用 `embed_out`。
- 设备拓扑同 `device_in` 的说明：双默认设备走 duplex（最低延迟）；指定设备或 loopback 组合走异步桥（环形缓冲解耦两台设备的时钟）。
- 设备周期 ≥10ms 且与图块长解耦，宿主负责切块调度。
- 全部参数 restart_required，运行时修改无效。
- 支持真实短块（不超过准备块长），输入有效帧数必须等于本次上下文帧数。
  错误入口、缺失/短输入、格式/通道/采样率不匹配和私有状态别名均显式拒绝。
  零帧不消费。宿主借用图输入缓冲，动态使用 `Runtime::get_input_buffer`，
  生成使用 `orpheus_host_device_out_buffer`；读取与推进须串行，不增加等待或排队。
- `device` 为最多1023字节的UTF-8名称；实际设备能力仍由宿主校验。

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

manifest 标记 `realtime_safe: true`：process 只校验，不修改音频位型，
无分配、锁或 IO；实际设备搬运由宿主负责。
