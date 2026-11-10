# orpheus.builtin.constant_source — 常量音频源

首批配置驱动组件壳示例。每次 Task 入口调用，输出 `value` 常量填充的完整音频块，
随后在算法中显式交付 `out`；不会由框架补调下游。

| 接口 | 契约 |
|---|---|
| out | 交错 f32 音频，`channels` 通道；驱动出口编号 1 |
| value | float，默认 1，可运行期修改 |
| channels | int，1–32，默认 2；改变后重建签名 |

接口事实源是 [component.yaml](./component.yaml)，算法在
[user/algorithm.c](./user/algorithm.c)，不要手改生成区。
仓库根执行 `python scripts\generate_component.py components\orpheus\builtin\constant_source\component.yaml`
重新生成组件壳。默认属于文件时钟域，采样率与块长来自工程。

无内存分配、阻塞或 IO；缺失输出 Buffer 会返回错误。
完整示例见 [interface_delivery_demo.yaml](../../../../examples/interface_delivery_demo.yaml)。
