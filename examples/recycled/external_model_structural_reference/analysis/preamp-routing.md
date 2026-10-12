# A.PREAMP_ROUTE_LEVEL 分支分析

状态：分支来源、默认路由和电平检测参数已提取；InnerLink 状态机与 golden 尚未完成。

## 为什么这条分叉重要

生成代码不是把 Volume、Tone、Balance 的最终结果直接送入主 PostProcess：

- 32 路 `Buffer` 从 `sf_rgainyprocess.audioOut` 取得，即 **Volume 后、Bass/Treble/Mid/Balance 前**。
- 10 路 `Buffer1` 从 `audioOut_f` 取得，即 **Tone/Balance 后**，送往 Audiopilot 和 Runtime B。
- 1 路 `Buffer2` 来自 InputSelect 的麦克风输出。
- 电平检测音频来自 InputMixer 的 10 路输出，经 72 样本延迟和每通道预加重；同时读取 Volume 当前增益并换算为 dB。

因此 Tone/Balance 对最终系统的影响主要经过自适应响度、跨 Runtime 计算和 22 路反馈闭环，而不是在同一帧直接串到主输出。把 Big6 画成一条简单串联主链会改变算法因果关系。

## 32 路后处理路由

默认 32 项映射为 `1..6` 循环后再接 `1,2`。RTC 中非零项可逐项覆盖；代码只把 1..6 视为有效来源，其他值最终整路置零。该规则与普通 10→32 channel router 不完全相同，后续实现必须保留“只允许前六路”的约束。

## 电平检测用途

预加重后的 10 路每帧计算：

- 每通道均方后取通道最大值，再转为 RMS dB；
- 全部样本绝对值的最大值，再转为 Peak dB。

两组 InnerLink 分别使用 Audiopilot 和 Dynamic EQ 的 decay、hold、volume 上下界更新慢速电平状态。`evidence/preamp-routing-defaults.json` 已保存参数与源指纹；状态递推仍需单独分析和 golden。
