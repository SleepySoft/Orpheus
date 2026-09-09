---
title: 为什么强调开放扩展
type: WHY
order: 5
up: '[[00-index]]'
tags: [orpheus/why]
---

# 为什么强调开放扩展

音频处理范围很宽，教学、算法研究、产品原型和嵌入式部署需要的组件集合不同。框架不应把扩展点绑定在单一业务形态上。

因此 Orpheus 采用开放边界：

- 组件以稳定 C ABI 接入。
- manifest 描述能力，Runtime 不感知业务细节。
- UI 控件和节点本体由注册表扩展。
- Transport、Bridge 和观测组件保持可替换。

