---
title: 教学与示例
type: HOW
order: 12
up: '[[00-index]]'
tags: [orpheus/how]
---

# 教学与示例

工程顶层 `lesson` 可声明步骤和结构检查。后端在扁平图和 Plan 上执行规则，UI 只对含教学配置的工程显示入口。

示例工程用于验证：

- 基础链路和增益控制。
- 采样率转换和多速率。
- 子组件、控制链路和异步桥。
- 平台 alter、频谱、扫频和绘图。

## 视频响度均衡与人声增强

`examples/video_loudness_voice.yaml` 面向电脑视频播放：

```text
系统声音 Loopback
	-> 节目响度均衡器
	-> [原声 / 人声中频+高频增强]
	-> n_way_mux 平滑选择
	-> limiter 峰值保护
	-> RMS/Peak Probe
	-> 耳机输出
```

- `loudness_normalizer` 是低延迟 RMS 节目电平器，不是带 K-weighting 和节目积分的标准 LUFS 计量；
- 默认目标 `-20 dBFS`，最大提升/衰减各 12 dB，低于 `-55 dBFS` 时保持增益以避免放大底噪；
- `voice_select.select=1` 旁路人声增强，`2` 启用 `midrange(+3 dB @ 2.6 kHz) + treble(+1.5 dB @ 5.5 kHz)`；
- `n_way_mux` 的交叉淡化提供通用子链旁路，不需要新增隐式运行模式；两条分支必须具有相同输出签名；
- Windows 实际使用建议让视频应用输出到虚拟声卡，再从 Orpheus 输出到物理耳机，防止 Loopback 捕获处理后输出形成回授。

