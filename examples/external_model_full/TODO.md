# 完整双 Runtime 模型 TODO

> 更新日期：2026-10-12
>
> 结构完成度以 `algorithm-index.yaml` 为准；模块验证进度以 `../recycled/external_model_structural_reference/distillation-progress.yaml` 的六门禁为准。本文只维护跨模块交接顺序和完成标准。

## 已完成

- [x] 建立 66 个递归算法定义和源文件指纹；
- [x] 索引 11 个 Task、17 个参数分区和 404 个字段；
- [x] 精确记录 A→B 488 words、B→A 1193 words 的字段与 offset；
- [x] 删除无算法行为的 BUS_EXCHANGE 节点和伪回环；
- [x] 补齐 2ch 输入、2→30 系统装配和 22ch 输出边界；
- [x] 将 Runtime 间事务按音频与控制/状态拆成四条只读语义链路；
- [x] 将块音频 shape 修正为 `[channels, frames]`；
- [x] 图上音频标签使用 channel × frame，控制标签使用字段语义；words/offset 仅作为事务详情；
- [x] 确保研究投影不会写入可执行 `graph.connections`；
- [x] placeholder 编译阻断、确定性生成和浏览器可见性测试。

## P0：先验证跨 Runtime 契约

- [ ] 找到 host transport / ping-pong / swap 的实际实现，确认 488/1193 words 是否整包原子切换；
- [ ] 确认生产、发布、消费发生在哪个 TID，以及同 tick 多 TID 的先后关系；
- [ ] 测量或推导 A→B、B→A 的块延迟，记录是否为同块、1 块或多块；
- [ ] 确认 `fast_sequence` 的递增、回绕、丢包和 reset 语义；
- [ ] 确认 enable/decay 与目标增益属于同一事务快照还是允许独立更新；
- [ ] 把结论写回 bus `atomicity`、Task phase 和 `unresolved`，附可证伪测试。

完成标准：可以画出精确的生产/交换/消费时序图，并用捕获数据验证至少 100 个连续事务无撕裂、无错序。

## P1：设计可执行 lowering

- [ ] 为音频字段选择跨 Task/跨 Runtime 数据面：评估 `async_bridge`、多端口结构化 bridge 或新的事务桥；
- [ ] 保证一条物理事务拆出的音频与控制字段仍共享 sequence/epoch，不因图上分线而失去一致性；
- [ ] 扩展运行时数组控制能力，支持 `[26]`、`[70]` 等 bulk 控制，不用标量 control link 冒充；
- [ ] 明确 1500 Hz Runtime A 与 Runtime B 多速率 Task 之间的 rate bridge、缓冲深度和欠载/溢出策略；
- [ ] 为动态 Runtime 和生成代码实现同一 lowering，并加入逐字节一致性测试；
- [ ] lowering 完成前保持当前语义边只读，不加入 `graph.connections`。

完成标准：动态/生成双路径在相同事务捕获下输出一致，sequence、epoch、音频块和控制数组同步。

## P1：逐模块蒸馏

按 `distillation-workflow.md` 的 boundary/schedule/algorithm/state/parameters/golden 六门禁推进：

- [ ] Runtime A 输入选择与 RTC 路由；
- [ ] Big6 全链及非默认参数；
- [ ] PreAmp、2→6 FDP、13→22 Mixing；
- [ ] PostProcess、保护、SoftClipper 与输出路由；
- [ ] Audiopilot HF/LF 估计、ramper 和 gain application；
- [ ] Runtime B Bands、降速 FDP、Mix Control/Deci Mixing；
- [ ] Peripheral/Headrest/Overhead HEQ 的运行期系数、enable/bypass 与延迟 golden；
- [ ] B PostHoligram / VLS 两行输出。

每个模块完成时必须同时交付：

- 分析记录与证据引用；
- 通用组件组合或独特算法组件；
- 参数提取脚本及资源 hash；
- 默认、非零控制、边界、跨块状态、reset、bypass golden；
- 独立可运行切片；
- 六门禁状态更新。

## P2：系统级验证

- [ ] 建立参考实现的 2ch 输入与 22ch 输出捕获工具；
- [ ] 覆盖 impulse、sweep、噪声、静音、极值控制和长时间 sequence；
- [ ] 分 Task 对拍中间字段，而不仅比较最终 22ch 输出；
- [ ] 验证 reset、mute、TOP/RTC 下载和模型切换事务；
- [ ] 记录容差来源，包括 accelerator FIR/IIR/FFT 的精度、舍入和 reduction order；
- [ ] 所有 placeholder 替换后再开放完整图编译。

完成标准：所有叶模块六门禁为 `verified`，完整图无 placeholder，动态/生成路径和参考实现通过约定 golden。

## P2：可理解性与工具

- [ ] 为 Runtime 间投影增加字段检查面板，替代仅依赖浏览器 tooltip；
- [ ] 支持按 audio/control/state/event 过滤和高亮字段生产者/消费者；
- [ ] 从顶层语义边跳转到对应递归定义和 Task；
- [ ] 在 UI 显示 `atomicity: unverified` 警告，而不是只放在 tooltip；
- [ ] 为项目增加教学检查：四条语义边、2→30 输入、22ch 输出、无 BUS_EXCHANGE 节点。

## 交接检查清单

1. 先读 `analysis/distillation-process.md`；
2. 再读权威 `algorithm-index.yaml` 和六门禁进度；
3. 修改生成源，不直接改生成文件；
4. 重生成索引与工程；
5. 跑 Algorithm IR 测试、前端测试、生产构建和全部示例校验；
6. 浏览器确认主链、四条 Runtime 间语义边、字段 tooltip 和图例；
7. 同步更新本文、分析记录、架构原则和实现日志。
