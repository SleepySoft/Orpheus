# HLOS Bridge Transport Adapter

> 状态：Python 主机侧 P0 已实现。支持 stdio、子进程双管道、TCP、Windows Named Pipe 与 POSIX Unix Domain Socket；统一 Endpoint 接入 Runtime/生成宿主仍按 `design_bridge_protocol.md` 的 P1/P2 推进。

## 1. 方向性与双工 Profile

Transport 的物理方向和 Bridge 的交互 Profile 是正交维度。

- 单根匿名 pipe/FIFO 通常是单向字节流；
- 子进程 `stdin + stdout` 两根 pipe 合起来是双向 Transport；
- Windows Named Pipe 可创建为双向；
- POSIX 本地实现使用双向 Unix Domain Socket；
- TCP socket 原生双向并支持同时收发。

上述所有双向 Transport 都默认运行 Bridge 半双工 Core Profile：同一时刻只有一个 outstanding CALL。只有能力选择 `duplex=full + pipelined_calls` 后，BridgeSession 才允许多个 CALL 并行等待响应。全双工仍对共享字节流做帧级写锁，避免不同请求的字节交错。

因此“Pipe 是否半双工”的准确答案是：**Pipe Adapter 提供双向承载；是否按半双工交互由 Bridge Profile 决定。**

## 2. 统一成帧

HLOS stream Transport 默认组合 `LengthPrefixCodec`：

```text
uint32_le frame_length + §18 message bytes
```

- frame length 包含完整 §18 消息，不包含 4 字节长度本身；
- 默认最大消息 4100 字节；
- 支持任意短读、粘包和连续多帧；
- 非法长度清空当前接收缓存并报错。

UART 保持使用 `OlinkCodec`（COBS + CRC16），因为串行链路需要定界、自同步和校验。Codec 与 Transport 可独立替换。

## 3. 已实现 Adapter

### 3.1 StreamTransport

把独立的二进制 reader/writer 组合为 `read/write/close`。它是 stdio、匿名 pipe 对和用户文件式 IPC 的基础适配器。

### 3.2 StdioTransport

包装当前进程的 `stdin.buffer/stdout.buffer`，供 HLOS Endpoint 进程使用。Windows 自动切换 binary mode。

stdout 一旦承载二进制 Bridge 帧，就不能混入 `printf`、JSON 或文本日志。日志必须写 stderr、异步 File Sink 或独立 Bridge Observation Lane。

### 3.3 ProcessTransport

启动一个 Endpoint 子进程，以父进程写 child stdin、读 child stdout 的双管道对承载 Bridge。负责进程 terminate/kill 和流关闭。

适合本机动态 Runtime、生成代码宿主及测试工具。当前 `rt_host`/`host_win` 尚未切换到二进制 Endpoint，因此该 Adapter 已可用于自定义 Endpoint，官方本机宿主迁移仍是下一阶段。

### 3.4 TcpTransport / TcpListener

已连接 TCP socket 的 Adapter 和轻量监听器：

- `TcpTransport.connect(host, port)` 用于 BridgeSession 主机侧；
- `TcpListener.accept()` 用于 HLOS Endpoint；
- 支持半双工 Core Profile和全双工流水化 Profile；
- 当前不内置 TLS、身份认证或自动重连，这些由后续安全 Session/部署层提供。

FastAPI 已支持连接已有 TCP Endpoint：

```json
{
  "target": "tcp",
  "host": "127.0.0.1",
  "network_port": 9400,
  "duplex": "half",
  "probe_interval_ms": 200
}
```

接口：`POST /api/projects/{name}/rt/start`。UI 暂未开放 TCP 输入控件。

### 3.5 LocalPipeTransport / LocalPipeListener

统一名称，本地按平台选择：

- Windows：`AF_PIPE`，地址如 `\\.\pipe\orpheus-debug`；
- POSIX：`AF_UNIX`，地址如 `/tmp/orpheus-debug.sock`。

FastAPI 可连接已有 Pipe Endpoint：

```json
{
  "target": "pipe",
  "pipe_address": "\\\\.\\pipe\\orpheus-debug",
  "duplex": "half",
  "probe_interval_ms": 200
}
```

省略地址时使用项目名经 `local_pipe_address(name)` 生成默认地址。当前 Windows/POSIX 实现基于 Python `multiprocessing.connection`，保留消息边界；上层仍使用统一 LengthPrefixCodec，不依赖该边界。

## 4. 用户替换 Adapter

`TransportAdapterRegistry` 保存名字到工厂的映射：

```python
registry = default_hlos_adapters()
registry.register("vendor_ipc", make_vendor_transport)
transport = registry.create("vendor_ipc", channel=3)
```

默认注册：

- `stdio`
- `process`
- `tcp`
- `pipe`

同名工厂只有显式 `replace=True` 才能替换。自定义 Adapter 只需实现：

```python
read(size: int) -> bytes
write(data: bytes) -> int
close() -> None
```

它不解析 route ID、CALL、Probe 或错误码。

FastAPI 的 `create_app(project_root, transport_adapters=...)` 接受整个注册表注入；REST TCP/Pipe 会话同样不硬编码具体 Adapter。

## 5. 日志

HLOS 可持久化运行日志，但日志 Sink 与 Transport 解耦：

- Bridge 二进制 stdout 不允许混入日志；
- `AsyncFileLogSink` 使用有界队列在后台线程写 UTF-8 文件；
- 音频 callback、组件 process 和 ISR 不执行文件 IO；
- stderr 可用于 Endpoint 启动失败等宿主级诊断；
- Probe/Observation 不写入生命周期日志文件。

## 6. 当前限制与后续计划

已完成：

- Python Adapter、监听器、长度前缀 Codec；
- 用户注册/替换机制；
- 同一 BridgeSession 对 stdio 子进程、TCP、Named Pipe/Unix Socket 的半双工 RPC 测试；
- TCP 全双工流水化测试；
- FastAPI TCP/Pipe 连接入口。

未完成：

1. C/C++ `BridgeEndpoint` 和 HELLO/IDENTITY；
2. `rt_host`、`host_win`、主动推进宿主的二进制 stdio/Pipe 接入；
3. TLS/系统凭证、自动重连、lease 和多客户端；
4. SHM 零拷贝 Observation、RPMsg 与多 Lane；
5. UI 的 TCP/Pipe Endpoint 选择控件。

在官方本机宿主完成 P2 前，现有文本 RtSession 仍存在；开发阶段不保留长期兼容，二进制 Endpoint 验收后直接删除。
