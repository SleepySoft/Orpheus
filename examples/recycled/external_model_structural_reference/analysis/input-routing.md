# A.INPUT_SELECT / A.INPUT_MIXER 分析记录

状态：边界、默认路由和混音权重已提取；RTC 事务与 golden 尚未完成，禁止并入完整系统图。

## 为什么先做输入路由

后续 Big6、Audiopilot 和 2→22 空间链依赖的不是原始 30 路输入，而是 InputSelect 先重排后得到的节目、麦克风和提示音集合。若直接把 2 路节目接到 Big6，会丢掉 30 路路由覆盖、无效通道静音和多声道布局切换。

## InputSelect 用途与顺序

1. 30 项 `routerOutMap` 先重排系统输入；默认是 1..30 原序。
2. RTC `routerMap` 的每个非零项覆盖同位置 TOP 值；零表示沿用 TOP。
3. 路由值不在 1..30 时，对应输出整帧置零，而不是复用边界通道。
4. 在重排结果上再选择节目 1..12、提示音 13..29、麦克风 30。

该顺序说明 InputSelect 不能简化为三个互不相关的 selector；运行期路由表作用于三类输出共同使用的中间 30 路总线。

## InputMixer 用途与模式

输入顺序是 7.1.4：`L,R,C,LFE,Lss,Rss,Lsr,Rsr,Ltf,Ltb,Rtf,Rtb`。输出固定为 10 路，但有两种并行计算路径：

- 默认 5.1.4：保留 L/R/C，给 LFE 加权，把 Lsr/Rsr 混入 Lss/Rss，并保留四路顶置。
- 立体声下混：把侧环绕和顶置分别混入 L/R，C 和 LFE 同时送往左右；输出通道 3..10 置零。

`InputMixer3dStereoDnMix` 决定选择哪条路径，当前默认值为 false，因此不能把 12→10 简写成固定立体声下混。

## 已落盘证据

`evidence/input-routing-defaults.json` 包含两份源指纹、所有默认路由、12/17/1 选择表、5.1.4 三个权重、立体声八个权重、makeup gain 和模式开关。它只证明默认参数与代码分支，不代表 RTC 更新或数值输出已经通过 golden。
