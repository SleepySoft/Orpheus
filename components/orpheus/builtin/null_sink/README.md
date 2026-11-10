# orpheus.builtin.null_sink — 空槽终端

## 功能
只有输入、无输出的安全终止组件。用于结束不需要输出的侧链（如 Audiopilot 的
level_detect 探针支路），避免下游组件收到 NULL 缓冲而崩溃。

## 端口
| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | `process: true`，上游交付时触发，输入音频直接丢弃 |

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
- 作为终点不续调；必需输入未绑定时返回错误。

## 配置驱动组件壳

使用 ABI v7，接口唯一事实源是 [component.yaml](./component.yaml)；
手写实现位于 [user/algorithm.c](./user/algorithm.c)，生成区不可手改。
仓库根执行 `python scripts\generate_component.py components\orpheus\builtin\null_sink\component.yaml`
重新生成组件壳；构建前会拒绝过期或被修改的生成物。
