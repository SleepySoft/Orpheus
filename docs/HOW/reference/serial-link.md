# 串行链路设计：PC 界面直连嵌入式设备调音（设计草案）

> 状态：历史 v1 已落地，2026-09-08 已纳入统一 Bridge Core Profile；当前双工、Session、Adapter 与日志契约以 `design_bridge_protocol.md` 为准。
> 目标：让本系统的 UI/后端经串口对运行**生成代码**的真实设备调音调参，且用户侧只需实现两个平台函数（send / onRecv）。

> 2026-09-08 更新：工程以顶层 `bridges` 为部署事实；`duplex=half` 默认使用单 outstanding CALL 和主机轮询，`duplex=full` 才允许主动 NOTIFICATION。下文保留部分 v1 历史描述用于解释演进，不作为当前协议依据。

## 0. 现状盘点（调查结论）

**已经有的（可直接复用）：**

- **消息信封**：8 字节头（route_id + bits：type/flags/call_id/payload_words）+ 4 字节对齐 payload，自描述长度（总长 = 8 + words×4），小端，上限 ~4KB。`docs/HOW/reference/registry.md` §18 定案。
- **解析分发**：动态路径 `Runtime::message()`（runtime.cpp:687）与生成路径 `orpheus_control_message()`（generator.py:1241）语义一致、均单入口；分发优先级 外部 hook → 组件 hook → 默认语义；CALL→同步 RESPONSE，NOTIFICATION 单向，错误位置 flags bit29。
- **ID 体系**：32 位 ID（kind/module/slot）+ id_map + 内存透明 resolve，动态/生成两路共用同一张表。
- **请求-响应匹配**：call_id 16 位，rt.py 已按此工作（`msg()` 按 call_id 等 MSGRSP）。
- **非音频组件通道**：`execution.none` 机制已通用（编译器收集进 plan.declarations、连线报错、cli build 跳过），只有生成器的模板分发硬编码 platform_hook（generator.py:719）。

**缺的（§18.2 明确"留给传输层"）：**

1. 帧界/同步：裸字节流上无法找帧头、错位后无法自恢复；
2. 完整性校验：无 CRC；
3. 转义：payload 为任意二进制；
4. 设备→主机方向的主动推送：runtime 只有 `build_notification`（构造函数），无 emit 通道；
5. 后端无任何传输层实现（无 pyserial/socket，唯一通道是 rt_host 子进程管道）；
6. HOW.md §8 的旧协议草案（另一套消息头 + OrpheusTransportInterface）已被 §17/§18 取代，以 §18 为准，§8 待修订。

## 1. 分层架构

```
┌─────────────────────────────────────────────────────────┐
│ UI（调参/探针/电平条）—— 完全不感知传输                    │
├─────────────────────────────────────────────────────────┤
│ L4 后端适配层（Python）                                   │
│   ControlPlane 抽象：LocalSession（rt_host 管道，现有）    │
│                     SerialSession（串口，新增，pyserial）  │
├─────────────────────────────────────────────────────────┤
│ L3 链路成帧层 OLINK（COBS + CRC16，纯 C + Python 双实现）   │
├─────────────────────────────────────────────────────────┤
│ L2 消息协议（§18 信封，已有）                              │
├─────────────────────────────────────────────────────────┤
│ L1 传输：stdio 管道（现有） / UART（用户实现 send/onRecv）  │
└─────────────────────────────────────────────────────────┘
```

关键：消息层（L2）一字节不动；OLINK（L3）是纯成帧/校验，不含消息语义；L4 对 UI 暴露与现有 rt 会话完全同形的接口。

## 2. L3 OLINK 帧格式（串口友好）

```
线上帧 = COBS编码( msg_bytes || crc16_le ) || 0x00
```

- **COBS**（Consistent Overhead Byte Stuffing）：0x00 做帧定界，天然同步恢复（任何错位最多丢一帧），无转义状态机，开销 ≤ 0.4%，MCU 上几十行实现。选它而不是 HDLC/0x7E 转义（实现更繁、开销更大）或裸流+长度前缀（无法自同步）。
- **CRC16-CCITT**（poly 0x1021，初值 0xFFFF）：对 ≤4KB 帧足够，MCU 有硬件 CRC 时可直用；校验失败静默丢帧（恢复靠上层 call_id 超时重发）。
- 消息自身已自描述长度（§18.2），故帧内不再加长度字段；crc 追加在消息尾部再整体 COBS。
- **可靠性语义**：链路层无 ACK/重传；请求-响应由 BridgeSession 使用 call_id 超时重试。半双工默认不接受主动 NOTIFICATION，Probe 由主机轮询；全双工主动 Observation 允许丢失。
- 双实现：`orpheus_runtime/src/olink.c`（C，同时被生成器复制进生成工程）与 `orpheus_core/orpheus_core/link/olink.py`（Python），互测帧级一致。

## 3. L4 后端设备适配层

- 统一主机接口为 `BridgeSession`：`stop/status/set_parameter/msg/write_id/read_id/write_bulk/read_bulk/map/resolve`。`SerialSession` 只是串口 Transport 与 OLINK Codec 的组合：
  - pyserial 打开串口（默认 921600 8N1，可配）；
  - 发送：OLINK 编码帧；接收：后台线程字节流→OLINK 解码→按 type 分派；半双工串行 CALL 并轮询 Probe，全双工按 call_id 匹配并接受主动 NOTIFICATION；
  - 超时重试、断链检测（写失败/读超时计数）。
- REST 扩展：
  - `GET /api/link/ports`：枚举串口（pyserial `list_ports`，仿照现有 `/api/devices` 缓存 30s）；
  - `POST /api/projects/{name}/rt/start` 增加 `{target: "local"|"serial", port, baud}`；serial 模式**不需要编译组件 DLL、不需要启动 rt_host**——直接对 plan.id_map 工作（计划只读）。
- UI：实时运行旁加目标选择（本地 / 串口下拉 + 波特率），其余 UI 零改动。
- 依赖：pyserial 加入 pyproject（导入失败时 serial 目标报清晰错误，不影响本地路径）。

## 4. 串口通信组件 `orpheus.builtin.uart_link`（非音频，拖入即加通信支持）

- manifest：`execution.none: true`、无 ports；参数：`link_name`、`baud`、`duplex`、`probe_interval_ms`（半双工轮询/全双工上报周期，0=关闭）与 `note`。
- 生成器：泛化 declarations 模板分发（消除 generator.py:719 的硬编码），manifest 新增 `codegen_template: uart_link` 字段（§21.2 预留位）。模板产出：
  - `orpheus_link_<name>.c/h`：OLINK 编解码 + 重组缓冲 + 收到完整帧即调 `orpheus_control_message()`，RESPONSE 经 send 回发；
  - `orpheus_link_hooks_<name>.c`：USER CODE 段，两个平台函数——
    - `int orpheus_link_<name>_send(const uint8_t* data, uint32_t len)`：链路层**调用方**，用户填（UART 阻塞写/DMA 入队均可，同步异步不限）；
    - 用户在自己的 UART RX 中断/回调（onRecv）里调 `orpheus_link_<name>_feed(const uint8_t* data, uint32_t len)`：响应式入口，与用户点名的风格一致。
  - `orpheus_link_<name>_poll()`：仅在 `duplex=full` 时按周期主动发送 Probe NOTIFICATION；半双工生成物中该周期为 0，由主机读 CALL 获取 Probe。
- 动态路径：组件不进执行计划，完全惰性（同 platform_hook）。

## 5. alter 语义问题的结论

**uart_link 不用 alter。** 分析：

- alter 组是**图节点级**的平台互斥（工程 YAML `alters:`，机制已落地，device_in[win] ↔ embed_in[dsp] 是范例）。它回答的是"同一条音频边，不同平台走哪个节点"。
- uart_link 没有音频边、不进执行计划，没有"按平台选路径"的问题——它只在生成路径有语义。
- "PC 上统一经过串口调试"由 **L4 后端适配层**承担（PC 根本不需要生成代码，pyserial 直连），不需要组件的 PC 实现。
- 若未来出现"PC 上跑着图、同时把控制面经串口桥给另一台设备"的嵌套场景，那时再给 `uart_link` 做一个 win/linux 真实实现组件（不同 id，如 `orpheus.builtin.uart_link_pc`），在工程里与原组件互为 `alters` 即可。**不建议**做"同 id 按 platforms 选实现"——registry 目前同 id 按版本号去重（registry.py:49），要支持需改注册语义，牵涉面大、收益小。

## 6. 实施顺序建议

1. OLINK 双实现 + 互测（纯算法，无外部依赖）；
2. 后端 SerialSession + `/api/link/ports` + rt/start target 参数 + UI 目标选择（先用 loopback 假串口或 com0com 自环验证）；
3. 生成器模板泛化 + uart_link 组件 + 生成工程在 PC 上以虚拟串口对跑通（生成代码侧 olink.c + 桩 send/feed）；
4. 半双工 Probe 轮询与全双工 NOTIFICATION 均与 UI 探针显示打通；
5. 真实设备联调；HOW.md §8 旧协议草案标记废止。

## 7. 明确不做（v1）

- 链路层重传/会话/流量控制；
- 同 id 多平台实现（registry 语义改动）；
- TCP/USB-CDC 以外的传输（OLINK 与传输无关，以后加 TCP 只是 L4 多一个 Session 类）；
- 修改 §18 消息信封。

## 8. 实现状态（2026-08-15，L1-L3 已落地）

| 层 | 状态 | 位置 |
|---|---|---|
| L2 消息信封/分发 | 已有，未动 | `orpheus_abi.h` / `Runtime::message` / `orpheus_control_message` |
| L3 OLINK C 实现 | ✅ | `orpheus_abi/include/orpheus_olink.h` + `orpheus_abi/src/olink.c`（纯 C99 无依赖，静态库 `orpheus_olink`；生成工程可直接复制源码） |
| L3 OLINK Python 实现 | ✅ | `orpheus_core/orpheus_core/link/olink.py`（`encode()` / `Decoder.feed()` 流式） |
| L1 PC 串口传输 | ✅ | `orpheus_core/orpheus_core/link/serial_port.py`（pyserial 薄封装，可选依赖，未装不影响本地路径） |
| L4 后端适配层 | ✅（2026-09-08 统一） | `bridge/`（BridgeSession + 能力/Codec/Transport/Log Sink）；`server/serial_session.py` 为 UART+OLINK 薄组合；半双工轮询与全双工通知均有测试 |
| 互测 | ✅ | `orpheus_core/tests/test_olink.py`（11 项：CRC 已知向量、COBS 无零、回环、逐字节流式、CRC 错丢帧重同步、垃圾自吞边界、空帧丢弃；C/Python 双向互测经 `tests/olink_cli.c` 按需现场编译驱动） |
| Access Bridge + 设备侧链路段 | ✅（2026-09-07 统一） | 顶层 `bridges` + `access_bridge` UI 投影；legacy `uart_link` 自动迁移。生成物：olink.c/h + `orpheus_link_<s>.c/h`（feed/poll）+ 平台 Adapter 桩 + `host_cli.c --link-stdio`；e2e 见 test_uart_link.py |

定案细节：
- 空消息帧（仅 CRC、无消息体）双实现一致丢弃——§18 消息最小 8 字节，长度 0 与「无帧」无法区分；
- 垃圾字节形成的伪 run 最多吞掉其后一帧，之后自动恢复（COBS 固语义，已测试钉死）；
- OLINK_MSG_MAX=4104；帧缓冲建议 ≥ OLINK_FRAME_MAX。
