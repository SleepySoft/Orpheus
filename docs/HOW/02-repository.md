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

## workspace 嵌套目录与 Git 工程

工程根目录以真实的 `project.yaml` 文件为准，必须位于 `workspace` 的子目录中，但不要求只嵌套一层。
可以直接把 Git 仓库 clone 到 `workspace`，也可以先按团队、产品等建立分组目录：

```text
workspace/
  demo/project.yaml
  team/
    cloned-repo/
      .git/
      examples/
        model-a/project.yaml
        model-b/project.yaml
```

界面工程列表递归发现这些工程，使用相对 workspace 的完整路径区分它们，
例如 `team/cloned-repo/examples/model-a`。不含 `project.yaml` 的中间目录不是工程；
同一仓库可以包含多个工程，父工程和子工程也都能被发现。新建工程时同样可以输入
`team/cloned-repo/models/demo`，支持中文、空格和版本号中的点。

- 工程内 imports、资源、输出、日志与生成代码仍相对各自的工程目录解析，不相对 Git 根目录解析。
- `.git` 等隐藏目录、`node_modules`、`__pycache__` 不参与扫描，目录符号链接和 Windows 目录联接不跟随。
- 工程路径不得包含绝对路径、空目录段、`.` / `..`、隐藏目录、Windows 保留名称或非法文件名字符。
- 工程信息读取失败时列表显示错误标记，不会静默把它当作正常工程。
- 删除操作仍删除整个工程目录；如果其中包含其它工程，则拒绝删除，须先移动或删除子工程。
  删除叶子工程不会删除它的父目录、Git 仓库元数据或兄弟工程。
- clone 完成或外部新增工程后，刷新页面即可重新发现；旧的单层工程保持兼容。

HTTP API 的工程名占一个 URL 段，为避免嵌套目录名与 `/components`、`/files` 等操作路由冲突，
该段用冒号表示目录分隔，再做 URL 编码。例如工程 `team/repo/demo` 的读取地址为
`/api/projects/team%3Arepo%3Ademo`，编译地址为 `/api/projects/team%3Arepo%3Ademo/compile`。
列表、请求 JSON 和界面中仍使用正常的 `/` 相对路径；单层工程 API 地址不变。

## 常用命令

```powershell
python serve.py
python -m orpheus_core.cli build
python -m orpheus_core.cli compile <project.yaml>
python -m orpheus_core.cli generate <project.yaml> <out_dir>
python -m pytest orpheus_core/tests/
cd ui; npm run build
```
