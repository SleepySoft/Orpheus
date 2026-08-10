# orpheus.builtin.channel_router — 通道索引路由

## 功能
按索引表把输入通道一对一映射到输出通道。输出通道 `o` 的样本来自输入通道 `indices[o]`；
索引越界或指定 `-1` 时该输出通道置零。

## 端口
| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | 输入音频 |
| out | output | audio | 输出音频 |

## 参数
| id | 类型 | 说明 |
|---|---|---|
| channels_in | int | 输入通道数（1~32） |
| channels_out | int | 输出通道数（1~32） |
| indices | string | 0-based 逗号分隔索引表，长度 ≥ channels_out |

## 示例
```yaml
component: orpheus.builtin.channel_router
params:
  channels_in: 22
  channels_out: 30
  indices: 2,6,0,7,3,1,11,13,12,10,8,4,9,5,18,19,21,20,17,16,14,15,-1,-1,-1,-1,-1,-1,-1,-1
```

## 实时安全
- 无堆分配、无阻塞、无文件/网络 IO。
- 支持 process 线程实时调用。
