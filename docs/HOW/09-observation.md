---
title: 观测与绘图
type: HOW
order: 9
up: '[[00-index]]'
tags: [orpheus/how]
---

# 观测与绘图

Probe 是运行期观测边界。组件不直接访问 UI 或文件，只把观测值交给宿主。

## 常见观测

- RMS、Peak。
- 波形、频谱、PSD。
- 相干矩阵、扫频响应。
- 控制值历史。

## 绘图

UI 使用共享 Plot 组件渲染波形和曲线。它统一坐标轴、网格、图例、十字光标和高分屏分辨率。

详见 [[reference/plot-widget|共用绘图组件设计]]。

