# orpheus.builtin.wav_in — WAV 文件输入

## 功能

把磁盘上的 WAV 文件作为图的音频源：启动（prepare）时**一次性把整个文件解码进内存**，之后每块（block）按顺序拷贝到输出端口。播放到文件末尾后**不循环**，后续块输出静音。

它是 `clock_source`（时钟源，`clock_domain: file`）：在离线/文件宿主里，整条链路由它驱动的时钟域按块推进。典型用途：离线批处理、算法验证、把参考素材灌入处理链再录回 WAV。

接口声明是事实源，独立生成入口：

```powershell
python scripts\generate_component.py components\orpheus\builtin\wav_in\component.yaml
```

Task只调用源，源填好缓冲后自主交付1号`out`并同步完成下游子树，
随后交付2号纯触发`tick`，可经触发分频器驱动组块/重采样组件的读取入口；
音频下游失败则不发tick，tick下游失败也不回滚已消费音频。
不使用旧next续调。尾块仍按请求量子补正零并交付完整块，不隐式改变时间线。
零帧交付零帧事件但不推进游标；reset只回到文件开头，不重新读盘。
下游失败不回滚已消费的游标；输出与实例状态或缓存音频重叠时明确拒绝。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| out | output | audio | 输出音频（interleaved f32），`channels` 通道 |
| tick | output | trigger | out子树成功返回后交付，无PCM；与out相同的帧位置/epoch/标志 |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `file_path` | string | `""` | WAV 文件路径；空 = 输出静音。restart_required（改后需重启） |
| `channels` | int | 2 | 输出通道数，1~64；影响端口签名（affects_signature），restart_required |

### `file_path` 的路径规则

- 相对路径相对**工程目录**解析（宿主进程的工作目录就是工程目录），绝对路径直接使用。
- Windows 下支持 UTF-8 中文路径（内部转宽字符 API 打开）。
- UI 中通过文件选择器（`widget: file`）填写。

### `channels` 与文件通道数不一致时

组件按 `channels` 参数输出，加载时做一次简单映射：

- 文件通道**少于** `channels`：多余的输出通道**重复第 0 通道**（不是补零）。
- 文件通道**多于** `channels`：多余的文件通道被丢弃。

## 注意事项

- **不做重采样**：文件自身的采样率被忽略（代码里读取后丢弃），样本按工程采样率原速逐点播放。文件采样率与工程采样率不一致时会**变速变调**（如 44.1k 文件在 48k 工程里会偏快偏高）。需要正确还原请用 `mp3_in`（它会重采样）或先把文件转成工程采样率。
- **支持的格式**：RIFF/WAVE中的PCM 16-bit、PCM 32-bit、IEEE float 32-bit。
  按块长度和奇数字节填充扫描，允许LIST/JUNK/bext等附加块及带扩展字节的fmt；
  不再假定数据固定在44字节之后。浮点载荷逐位复制，保留NaN/Inf/负零。
  8/24-bit PCM、压缩或Extensible等尚未实现的格式明确拒绝，不再成功输出静音。
  文件截断、零声道/位宽、无效采样率/字节率、块对齐错误、残缺帧及重复fmt/data均报错；
  所有长度和除数在分配/解码前校验。空data是合法静音文件。
- **容量上限**：总帧数不得超过 16M（48kHz 下约 5.8 分钟，与 `mp3_in` 口径一致），超出启动报错。
- **不循环**：播完即静音。需要循环请在工程层重复触发或改用其他源。
- 所有参数都是 restart_required，运行时 SET 参数会返回不支持。
- 无设备批处理时，服务器按 WAV 总帧数估算块数，处理完整文件后自动停止；全速模式不等待实际播放时长。

## 典型用法

```yaml
component: orpheus.builtin.wav_in
params:
  file_path: in.wav     # 相对工程目录
  channels: 2
```

```
wav_in ──► gain ──► wav_out          # 离线链路：读 WAV → 处理 → 写 WAV
```

## 实时安全

manifest 标记 `realtime_safe: false`：文件 IO 和内存分配发生在 prepare（启动时一次性完成），`process` 本身只做内存清零 + 拷贝。虽然 process 实质是安全的，但语义上它属于文件时钟域组件，不应放进设备实时链路。
