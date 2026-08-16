# orpheus.builtin.uart_link — 串口链路（生成代码的串口控制通道）

## 功能

**非音频组件**：不进执行计划、没有端口、不能连线。把它拖入工程，「生成独立 C 工程」时就会在产物里加入一条 **OLINK 串口控制链路**：

- **成帧**：COBS + CRC16（OLINK，见 `orpheus_olink.h`），自同步、抗干扰；
- **分发**：收到的完整帧交给生成工程自带的 `orpheus_control_message()`——标量/BULK 读写、探针只读、CUSTOM hook，与 PC 动态路径语义完全一致；
- **探针泵**：按 `probe_interval_ms` 周期把工程里所有 PROBE 数据点以 NOTIFICATION 帧主动发出。

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
orpheus_link_<名>_poll(HAL_GetTick());       /* 主循环周期调用（探针泵心跳） */
```

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `link_name` | string | `""` | C 符号前缀（缺省用节点 id）；决定生成函数名 `orpheus_link_<前缀>_*` |
| `baud` | int | 921600 | 仅作意图声明写进生成代码注释，真实波特率由你的串口初始化决定 |
| `probe_interval_ms` | float | 200.0 | 探针上行周期（毫秒）；0 = 关闭探针泵 |
| `note` | string | `""` | 自由备注，写进生成代码注释 |

## PC 冒烟（无硬件验证整条链路）

生成的 `orpheus_generated_app` 带 `--link-stdio` 模式：stdin/stdout 就是链路（二进制模式）：

```
orpheus_generated_app --link-stdio
```

此时 hooks 里的 `send` 默认实现为 `fwrite(stdout)`（`ORPHEUS_LINK_STDIO` 已在 CMake 定义），主循环自动跑图块、喂 stdin、驱动探针泵。Python 侧（`orpheus_core.server.serial_session.SerialSession`）接管道即可端到端调通，测试 `orpheus_core/tests/test_uart_link.py` 就是这么做的。

## 注意事项

- 不能连线（编译期报错）；只在代码生成路径有意义，动态路径完全惰性。
- 重新 `generate` 会覆盖 `src/orpheus_link_<名>.c`（生成物），但 `orpheus_link_hooks_<名>.c` 的 USER CODE 段属于你的实现——重新生成也会覆盖整个文件，请另存副本（与 platform_io.c 同一约定）。
- 链路层无重传：CRC 错丢帧，靠 PC 侧 call_id 超时重发兜底；NOTIFICATION 探针允许丢失（周期性的）。
- 一个工程可放多个 uart_link 节点（各自独立实例，符号前缀不同）。
