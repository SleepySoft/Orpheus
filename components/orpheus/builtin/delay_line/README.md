# orpheus.builtin.delay_line — 多通道延迟线

## 功能
每通道独立延迟样本数的延迟线。支持最大 192000 样本（约 4s @48kHz），
适用于 SpeakerDelay、TrebleDelay、TrebleSurroundDelay 等大延迟场景。

## 端口
| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | 输入音频 |
| out | output | audio | 延迟后音频 |

## 参数
| id | 类型 | 说明 |
|---|---|---|
| channels | int | 通道数（1~32） |
| max_delay_samples | int | 最大延迟样本数（1~192000） |
| delays_samples | string | 每通道延迟样本数，逗号分隔，长度 ≥ channels |

## 示例
```yaml
component: orpheus.builtin.delay_line
params:
  channels: 22
  max_delay_samples: 32384
  delays_samples: 1280,1280,...,1280
```

## 实时安全
- 内部使用环形缓冲，process 中无堆分配。
- 无阻塞、无文件/网络 IO。
