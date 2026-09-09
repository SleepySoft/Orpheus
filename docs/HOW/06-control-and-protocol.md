---
title: 控制与协议
type: HOW
order: 6
up: '[[00-index]]'
tags: [orpheus/how]
---

# 控制与协议

## 参数

- 参数有类型、默认值、范围、更新策略和签名影响。
- `bindable` 参数可作为控制目标。
- `control_source` 参数必须可读。

## 控制链路

控制链路从源参数读值，在块边界写到目标参数。闭环允许，但语义是每链一块延迟。

## 消息与 Bridge

- Bridge 抽象本机、串口和其他访问端点。
- OLINK 成帧使用 COBS 与 CRC16。
- HLOS Transport 支持 stdio、process、TCP 和本地 pipe。

详细资料见 [[reference/control-links|控制链路]]、[[reference/bridge-protocol|Bridge 协议]] 和 [[reference/hlos-transport|HLOS Transport]]。

