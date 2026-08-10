# orpheus.builtin.null_sink — 空槽终端

## 功能
只有输入、无输出的安全终止组件。用于结束不需要输出的侧链（如 Audiopilot 的
level_detect 探针支路），避免下游组件收到 NULL 缓冲而崩溃。

## 端口
| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | 输入音频（直接丢弃） |

## 参数
| id | 类型 | 说明 |
|---|---|---|
| channels | int | 输入通道数（1~32） |

## 示例
```yaml
component: orpheus.builtin.null_sink
params:
  channels: 10
```

## 实时安全
- process 中仅判空并返回，无计算、无 IO、无阻塞。
