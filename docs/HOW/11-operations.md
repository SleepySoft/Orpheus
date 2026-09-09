---
title: 运行与部署
type: HOW
order: 11
up: '[[00-index]]'
tags: [orpheus/how]
---

# 运行与部署

## 本机服务

`python serve.py` 启动 API 和 UI。

## 动态运行

UI 编译工程后交由 Runtime 加载组件执行。

## 生成运行

生成工程可以独立构建。`win` 宿主连接声卡；`dsp` 宿主由平台适配层接入真实 IO。

## 远程访问

Bridge 支持本机会话、串口、TCP、pipe 和 process 等 Transport。

