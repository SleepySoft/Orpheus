# orpheus.builtin.channel_router — 通道索引路由

## 功能
按索引表把输入通道一对一映射到输出通道。输出通道 `o` 的样本来自输入通道 `indices[o]`；
负索引或越界时该输出通道置正零。有效映射逐字节拷贝，保留 NaN 载荷和负零位型。

## 端口
| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | `process: true`，每次实际交付立即处理 |
| out | output | audio | `drive_port_no: 1`，填好输出后同步交付 |

## 参数
| id | 类型 | 说明 |
|---|---|---|
| channels_in | int | 输入通道数（1~64） |
| channels_out | int | 输出通道数（1~64） |
| indices | string | 空字符串为顺序映射；非空必须恰好 channels_out 个 int32，0 起点，允许空白 |

不再接受忽略垃圾字符、截断或不足项自动补齐的 CSV；格式错误明确拒绝，
候选配置校验失败不修改既有映射。reset 保留映射。
输入必需，空输出允许；零量子不发射，输入输出重叠明确拒绝。
接口以 `component.yaml` 为事实源，生成壳与手写算法分别位于 `generated/` 和 `user/`。

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
