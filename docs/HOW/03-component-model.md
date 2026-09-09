---
title: 组件模型与 ABI
type: HOW
order: 3
up: '[[00-index]]'
tags: [orpheus/how]
---

# 组件模型与 ABI

组件由 manifest 和 C/C++ 实现组成。

## manifest 能力

- 声明 id、名称、类别和源码。
- 声明端口、参数、内存、平台和执行属性。
- 参数可影响签名，也可 bindable 作为控制目标。
- 端口可以引用参数实现可变通道数。

## C ABI 边界

- Runtime 通过 descriptor、ports、parameters 和 process 访问组件。
- 入口符号使用统一宏导出，避免静态链接冲突。
- 组件 process 中禁止动态分配、阻塞锁、文件网络 IO 和异常传播。

详细说明见 [[reference/registry|组件注册与 ABI 细节]]。

