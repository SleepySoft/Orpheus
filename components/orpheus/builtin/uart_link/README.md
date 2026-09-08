# orpheus.builtin.uart_link — 串口链路（生成代码的串口控制通道）

> 兼容入口：新工程请使用「访问桥」配置节点。旧 `uart_link` 在编译时自动映射为顶层 `bridges[].transport: uart`，首次经 UI 保存后完成迁移。

## 功能

**非音频组件**：不进执行计划、没有端口、不能连线。把它拖入工程，「生成独立 C 工程」时就会在产物里加入一条 **OLINK 串口控制链路**：

- **成帧**：COBS + CRC16（OLINK，见 `orpheus_olink.h`），自同步、抗干扰；
- **分发**：收到的完整帧交给生成工程自带的 `orpheus_control_message()`——标量/BULK 读写、探针只读、CUSTOM hook，与 PC 动态路径语义完全一致；
- **观测**：半双工由主机按 `probe_interval_ms` 串行读取 PROBE；全双工才由设备按该周期主动发送 NOTIFICATION。

于是 PC 上 Orpheus 界面（运行目标选「串口」）就能对跑生成代码的真实设备调音调参、看探针。

## 你要写的只有两处代码

生成工程里的 `src/orpheus_link_hooks_<名>.c`（USER CODE 段）：

```c
/* 1. 发送：链路层需要发字节时调用它（同步/异步/DMA 随你） */
int32_t orpheus_link_<名>_send(const uint8_t* data, uint32_t len);

/* 2. 初始化：串口外设/DMA 初始化（orpheus_generated_init 尾部自动调用） */
void orpheus_link_<名>_init(void);
```

然后在**你自己的串口接收路径**（中断/DMA 回调/轮询读取处）调用：

```c
orpheus_link_<名>_feed(rx_buf, rx_len);      /* 收到的字节喂给链路层 */
orpheus_link_<名>_poll(HAL_GetTick());       /* 全双工主动观测时周期调用 */
```

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `link_name` | string | `""` | C 符号前缀（缺省用节点 id）；决定生成函数名 `orpheus_link_<前缀>_*` |
| `baud` | int | 921600 | 仅作意图声明写进生成代码注释，真实波特率由你的串口初始化决定 |
| `duplex` | string | `half` | `half`=半双工兼容基线；`full`=允许主动通知和请求流水化 |
| `probe_interval_ms` | float | 200.0 | 半双工主机轮询/全双工主动上报周期（毫秒）；0 = 关闭周期观测 |
| `note` | string | `""` | 自由备注，写进生成代码注释 |

## PC 冒烟（无硬件验证整条链路）

生成的 `orpheus_generated_cli` 带 `--link-stdio` 模式：stdin/stdout 就是链路（二进制模式）：

```
orpheus_generated_cli --link-stdio
```

此时 hooks 里的 `send` 默认实现为 `fwrite(stdout)`（`ORPHEUS_LINK_STDIO` 已在 CMake 定义），CLI 宿主自动跑图块并喂 stdin。Python 侧通过统一 `BridgeSession` 接管该字节通道；默认半双工由主机轮询 Probe，测试 `orpheus_core/tests/test_uart_link.py` 覆盖完整链路。

## 注意事项

- 不能连线（编译期报错）；只在代码生成路径有意义，动态路径完全惰性。
- 重新 `generate` 会覆盖 `src/orpheus_link_<名>.c`（生成物），但 `orpheus_link_hooks_<名>.c` 的 USER CODE 段属于你的实现——重新生成也会覆盖整个文件，请另存副本（与 platform_io.c 同一约定）。
- 链路层无重传：CRC 错丢帧，靠 PC 侧 call_id 超时重发兜底；半双工一次只有一个 CALL，全双工 NOTIFICATION 允许丢失。
- 一个工程可放多个 uart_link 节点（各自独立实例，符号前缀不同）。
