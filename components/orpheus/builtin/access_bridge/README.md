# 访问桥

无音频端口的声明式配置节点。它在画布上提供外部访问 Bridge 的显式入口，保存时写入工程顶层 `bridges`，不进入音频执行计划。

当前支持 `uart + olink`：生成工程自动加入 OLINK Endpoint 和 UART 平台钩子。`duplex=half` 是兼容基线，主机用单 outstanding CALL 轮询 Probe；`duplex=full` 才启用设备主动 Probe 与请求流水化。`resource` 是逻辑资源名，实际 UART 驱动由目标平台 Adapter 绑定。

工程节点配置的是设备侧逻辑资源（如 `uart2`）；工具栏选择的是 PC 本次连接端点（如 `COM5`），两者不会混入同一份可移植配置。目标平台已有 Adapter 时自动绑定；未知平台才使用 callback 桩。

旧 `orpheus.builtin.uart_link` 节点会自动兼容迁移为同一 Bridge 声明。
