# orpheus.builtin.wav_out — WAV 文件输出

## 功能

把输入音频累积写入 WAV 文件。它是离线运行（文件宿主）时的典型终点组件：所有实时处理完成后，最终混音结果交给 `wav_out` 落盘。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `file_path` | string | "" | 输出 WAV 文件路径。 |
| `channels` | int | 2 | 输入通道数，改变后需重新编译。 |
| `sample_rate` | int | 48000 | 写入 WAV 的采样率。 |

### `file_path` 的真实含义

文件路径支持相对路径（相对于 Runtime 工作目录）和绝对路径。如果路径中包含不存在的目录，组件会在 `destroy` 时自动创建目录。

示例：`file_path: "outputs/demo.wav"` 会在工程目录下创建 `outputs/` 目录并写入 `demo.wav`。

### 输出格式

- 容器：RIFF/WAVE
- 位深：16-bit 有符号整数 PCM
- 采样率：由 `sample_rate` 参数决定
- 通道数：由 `channels` 参数决定

输入样本在写入前会被钳位到 `[-1.0, +1.0]`，再乘以 32767 转成 int16。因此超过 0 dBFS 的信号会被硬削波。

## 端口

- `in`: 输入音频，`channels` 通道。该组件没有输出端口。

## 注意事项

- **不是实时安全组件**：process 中不做文件 IO，但 prepare 时分配大缓冲区，destroy 时一次性写文件。因此不能用于实时设备宿主（rt_host）。
- 最大缓存样本数：约 1600 万个样本（`1024*1024*16`）。对于 48kHz 立体声，约为 3 分钟；超限后新的样本会被丢弃。
- 空路径时不会写文件，但不会报错。
- 写入是“一次性”的：如果工程运行结束后没有正常 destroy（例如进程被强制终止），WAV 可能不会被写出。

## 典型用法

```
main_chain ──► wav_out(file_path="baf_step0_main_out.wav",
                       channels=22, sample_rate=48000)
```

把 22 通道主输出写入 WAV，方便后续在 DAW 中回放或分析。
