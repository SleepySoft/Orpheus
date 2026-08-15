# orpheus.builtin.mp3_in — MP3 文件输入

## 功能

把 MP3 文件作为图的音频源：启动（prepare）时用 miniaudio 解码器**一次性把整个文件解码成 f32 存进内存**，之后每块按顺序输出。解码时**按图采样率自动重采样、按 `channels` 自动做声道转换**，所以不存在 `wav_in` 那种变速变调问题。播完不循环，输出静音。

它是 `clock_source`（`clock_domain: file`），用于离线/文件时钟域链路。典型用途：直接拿现成音乐素材做处理链验证，免去先转 WAV 的步骤。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| out | output | audio | 解码后的音频（interleaved f32），`channels` 通道 |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `file_path` | string | `""` | MP3 文件路径（UI 文件选择器按 `.mp3` 过滤）；空 = 输出静音。restart_required |
| `channels` | int | 2 | 输出通道数，1~32；解码器直接转成该声道数。affects_signature，restart_required |
| `total_frames` | int | 0 | 解码得到的总帧数（readback 探针，只读） |

### 与 `wav_in` 的关键区别

| | wav_in | mp3_in |
|---|---|---|
| 采样率 | 忽略文件采样率，按工程速率原速播放（会变速变调） | 解码时重采样到图采样率，音调正确 |
| 声道处理 | 简单映射（少通道重复第 0 通道） | miniaudio 标准声道转换 |
| 容量上限 | 16M **样本**（跨通道合计） | 16M **帧**（约 48kHz 5.8 分钟），超出部分**截断** |

### `total_frames` 探针

启动后可以从该探针读到实际解码帧数，配合工程采样率即可换算时长（`total_frames / sample_rate` 秒）。若读到的值明显小于预期，说明触到了 16M 帧上限被截断。

## 注意事项

- 路径规则与 `wav_in` 相同：相对路径相对工程目录，支持中文路径。
- 文件读不开 / 解码失败时 prepare 返回 NOT_FOUND，运行直接失败——先确认文件存在。
- 虽然 UI 按 `.mp3` 过滤，miniaudio 实际也能解码 WAV/FLAC 等格式（并自动重采样），必要时可以当"通用音频文件输入 + 重采样"用。
- 整个文件解码进内存：16M 帧 × 32 通道 × 4 字节的极端配置会占用大量内存，长素材请控制通道数。
- 全部可写参数 restart_required，运行时不能换文件。

## 典型用法

```yaml
component: orpheus.builtin.mp3_in
params:
  file_path: demo.mp3
  channels: 2
```

```
mp3_in ──► level_detect ──► device_out     # 把 MP3 灌进实时播放链（注意时钟域：
                                           # 实时链路以 device 为时钟，文件源可混入）
```

## 实时安全

manifest 标记 `realtime_safe: false`：解码与内存分配都在 prepare 完成，`process` 只做清零 + 拷贝。语义上属于文件时钟域组件。
