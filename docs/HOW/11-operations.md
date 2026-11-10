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

服务使用单进程单写者编辑租约，不支持同一工作区启动多个服务进程或多个
uvicorn worker。不同进程的内存租约互不协调，不能用多进程部署替代单服务入口。
重启服务会作废所有旧令牌，已有网页需「接管并刷新」才能继续写入。

### 外部 HTTP 写客户端

所有 API 写入须先申请工作区租约，并在请求头携带 `X-Orpheus-Edit-Token`。
例如 PowerShell：

```powershell
$base = 'http://127.0.0.1:8000/api'
$lease = Invoke-RestMethod -Method Post "$base/editor/acquire" -ContentType 'application/json' -Body '{"takeover":false}'
$headers = @{ 'X-Orpheus-Edit-Token' = $lease.token }
# 写操作传入 -Headers $headers；长会话每 10 秒向 editor/renew POST 续租。
Invoke-RestMethod -Method Post "$base/editor/release" -Headers $headers
```

- `GET /api/editor/lease` 查询占用状态，不返回持有者令牌。
- `POST /api/editor/acquire`：`takeover=false` 在有其他持有者时返回 409；
  显式 `takeover=true` 撤销旧租约。申请/接管后必须重新读取工程，不能保存旧快照。
- `POST /api/editor/renew`、`POST /api/editor/release` 使用请求头中的当前令牌；
  过期或失权返回 409，`code=edit_lease_lost`，旧释放不能释放新持有者。
- 读取（包括接口预览、教学检查和实时读回 POST）不需要编辑权。

租约用于防止误覆盖，不是用户认证或多用户协同协议。

## 动态运行

UI 编译工程后交由 Runtime 加载组件执行。

## 生成运行

生成工程可以独立构建。`win` 宿主连接声卡；`dsp` 宿主由平台适配层接入真实 IO。

## 远程访问

Bridge 支持本机会话、串口、TCP、pipe 和 process 等 Transport。
