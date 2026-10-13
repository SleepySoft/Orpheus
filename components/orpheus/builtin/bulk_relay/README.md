# bulk_relay - BULK 控制中继

## 功能

保存一组 float 控制值，并同时作为标准 `control_connections` 的 BULK 目标和控制源。用于数组状态快照、同 Task 模块间目标矩阵传递和显式多跳控制；不承载音频。

数组在块末尾进入目标 shadow，下一个块开始前一次性提交。`count` 是实例有效长度，底层槽固定容量 2048；未使用后缀保持原值。

## 参数

| 参数 | 说明 |
|---|---|
| `count` | 有效元素数，1..2048，影响控制 shape |
| `initial_values` | 冷启动数组，逗号分隔 |
| `values` | 标准 BULK 控制端点，映射到 `values_storage` |

该组件不替代跨 Task 或跨 Runtime 事务桥；这两类控制仍需显式 bridge 和 sequence/epoch 契约。