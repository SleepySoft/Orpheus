# orpheus.builtin.linear_gain — 线性增益

首批配置驱动组件壳示例。`out = in × gain`，处理完成后由算法显式交付输出；
不包含 dB 转换、平滑或限幅。

| 接口 | 契约 |
|---|---|
| in | 交错 f32 音频，`process: true`，交付时触发算法 |
| out | 同规格音频；驱动出口编号 1，设计比例 1/1 仅供显示 |
| gain | float，默认 1，可运行期修改 |
| channels | int，1–32，默认 2；改变后重建签名 |

接口事实源是 [component.yaml](./component.yaml)，算法在
[user/algorithm.c](./user/algorithm.c)，不要手改生成区。
仓库根执行 `python scripts\generate_component.py components\orpheus\builtin\linear_gain\component.yaml`
重新生成组件壳。

无内存分配、阻塞或 IO；必需 Buffer 缺失、帧数不足或通道不匹配返回错误。
完整示例见 [interface_delivery_demo.yaml](../../../../examples/interface_delivery_demo.yaml)。
