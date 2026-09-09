---
title: 编辑器与交互
type: HOW
order: 8
up: '[[00-index]]'
tags: [orpheus/how]
---

# 编辑器与交互

UI 使用 React 和 React Flow。

## 状态

- 视图状态负责画布位置、选区和展开。
- 工程状态负责节点、边、参数和配置。
- 组件目录来自后端 registry。

## 扩展

- 参数控件走 `widgets.js` 注册表。
- 节点本体走 `nodeWidgets.js` 注册表。
- 控制边、替代组和 Bridge 投影由专用组件处理。

组件 UI 设计见 [[reference/component-ui|组件自定义 UI]]。

