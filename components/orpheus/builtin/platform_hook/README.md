# orpheus.builtin.platform_hook — 平台资源钩子

## 功能

**声明式平台集成节点**：在画布上声明一项非音频平台资源（amixer 控件、通信收发、传感器等），它**不参与运行时执行**，只在"代码生成路径"（`cli generate`）中产出 `init/read/write` 钩子函数，由用户在 USER CODE 段填充硬件相关实现。是嵌入式部署路径上控制面的占位机制（design_registry §21，控制面 §20 完整落地前的过渡层）。

### 它是如何工作的

1. **无源码、无端口**：本组件目录只有 `component.yaml`，manifest 声明 `execution.none: true`。
2. **编译期**：`execution.none` 节点不进入执行计划，仅收集进 `plan.declarations`；给它连线会报错（它没有任何端口可连）。平台解析时它也不约束平台可达性（不会因为 PC 上没有该资源而卡住编译）。
3. **构建期**：`cli build` 跳过此类组件——没有运行时代码可构建。
4. **生成期**：生成器产出两个文件并纳入 CMakeLists 编译：
   - `include/orpheus_platform_hooks.h`：每个钩子的函数声明；
   - `src/platform_hooks.c`：空实现 + `USER CODE BEGIN/END` 段，用户按硬件填充。

每个钩子生成三个函数（符号名取 `hook_name` 经标识符净化，缺省回退节点 id）：

```c
void orpheus_platform_<name>_init(void);        /* 初始化：打开设备/注册控件 */
int  orpheus_platform_<name>_read(float* value);  /* 读：传感器/控件当前值 */
int  orpheus_platform_<name>_write(float value);  /* 写：设置控件/发送数据 */
```

你的应用程序 `include "orpheus_platform_hooks.h"` 后自行决定何时调用这些钩子（如定时轮询 read、状态变化时 write）。

## 端口

无端口。该节点不参与图的数据流，不能连线。

## 参数

| 参数 | 类型 | 默认值 | update_policy | 说明 |
|---|---|---|---|---|
| `hook_name` | string | `"platform_hook"` | restart_required | 钩子名：决定生成的 C 符号 `orpheus_platform_<hook_name>_*` |
| `interface` | string | `"generic"` | restart_required | 接口标签（如 `amixer`/`uart`/`sensor`）：写入生成文件注释，提示填充者该钩子的用途 |
| `note` | string | `""` | restart_required | 自由说明文本：同样原样写进生成代码注释 |

## 注意事项

- **只在代码生成路径有意义**：UI「▶ 运行」（动态加载路径）下该节点完全惰性——不加载、不调用、不占资源。
- `platform_hooks.c` 由生成器**整体覆盖**：重新 generate 会丢失 USER CODE 段里的填充内容，生成文件头部已注明"请另存副本"。建议把实现写在单独的 `.c` 文件里、或在填充后备份。
- `hook_name` 会被净化为合法 C 标识符；多个平台节点请使用不同的 `hook_name`，否则符号冲突。
- v1 仅支持"无音频输入的纯声明节点"；design_registry §21 还规划了"有音频输入的观测汇"形态（no-op 汇 + PROBE 观测），尚未落地。

## 典型用法

嵌入式工程里需要把音频图的部署和一个 ALSA 音量控件绑定：

1. 画布放一个 `platform_hook`，设 `hook_name = "master_volume"`、`interface = "amixer"`、`note = "主音量控件"`；
2. `cli generate` 生成独立 C 工程；
3. 在 `src/platform_hooks.c` 的 USER CODE 段（或另存的副本）里实现 `orpheus_platform_master_volume_init/read/write`（调用 `snd_mixer_*` API）；
4. 应用主程序按自己的节奏调用这些钩子。

## 实时安全

节点本身无 process、无运行时代码，`realtime_safe: true`；**填充的 USER CODE 是否实时安全由用户负责**——不要在音频回调线程里调用做了阻塞 IO 的钩子。
