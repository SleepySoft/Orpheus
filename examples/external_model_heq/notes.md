# External Model Headrest / Overhead HEQ 蒸馏

这是外部算法蒸馏的第一个可执行切片，不是结构占位。两个复合组件均可展开为基础组件图。

- Headrest：10 路输入映射到 40 个 530-tap FIR；默认每个 filter 的 tap 529 为 1.0，输出 40 路。
- Overhead：20 路输入映射到 20 个 530-tap FIR；默认每个 filter 的 tap 529 为 0.2，按 `[0,5,10,15]` 分为四组求和。
- 两条链都复现生成代码的两样本跨块 carry，总默认延迟为 531 个本地域样本。
- 系数来自 `Model_1_2_PreAmp_p10_b1_TOP.c` 和 `p9_b1_TOP.c`，由 `scripts/extract_external_heq.py` 生成带 SHA-256 的 `f32le` 资源与复合图。

当前 p9/p10 PoolIIR 默认系数全零，因此 FIR 主支路就是当前 TOP 默认输出的有效算法部分。后续阶段补齐运行期 room-mode IIR 更新与 enable/bypass。