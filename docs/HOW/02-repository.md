---
title: 仓库结构与工作流
type: HOW
order: 2
up: '[[00-index]]'
tags: [orpheus/how]
---

# 仓库结构与工作流

## 主要目录

| 路径 | 内容 |
|---|---|
| `components/orpheus/builtin` | 内置组件源码与 manifest |
| `orpheus_abi` | C ABI 契约 |
| `orpheus_core` | 编译、生成、服务和测试 |
| `orpheus_runtime` | 动态 Runtime 与宿主 |
| `ui/src` | React 图编辑器 |
| `examples` | 可导入示例 |
| `workspace` | 用户工程 |

## 常用命令

```powershell
python serve.py
python -m orpheus_core.cli build
python -m orpheus_core.cli compile <project.yaml>
python -m orpheus_core.cli generate <project.yaml> <out_dir>
python -m pytest orpheus_core/tests/
cd ui; npm run build
```

