# Orpheus 架构参考

## 目录

- 两种执行模式（核心概念）
- 数据流：YAML → plan.json → 执行
- C ABI 要点
- Runtime 宿主分工
- 后端 API 面
- 前端结构

## 两种执行模式（核心概念）

运行方式只有两种；**WAV 还是设备音频是图的输入输出组件决定的，与执行模式无关**（可自由组合：系统声音→处理→WAV = 录制）。

1. **基座动态加载**（UI「▶ 运行」）：图编译只产出 plan.json 数据；组件 DLL 预编译；基座程序 LoadLibrary 加载后经 ABI 函数表调用。图改动零 C 编译。
2. **代码生成**（UI「⚙ 编译后运行」/ `orpheus-cli generate`）：`CodeGenerator` 展开为自包含 C 工程（组件源码 + 生成的 main.c + vendored orpheus_abi.h），静态编译运行。面向嵌入式部署。限制：单 Task、无探针、不支持设备组件。

设计原则：两种模式输出必须一致。自动化保障：`orpheus_core/tests/test_server_devices_files.py::test_generated_run_matches_dynamic_run` 逐字节比较输出 WAV。

## 数据流

```
project.yaml ──ProjectLoader──> Project（含 subcomponents）
  ──flatten_project──> 纯原子图（sub: 实例递归展开，节点 id 加 "实例__" 前缀）
  ──GraphCompiler.compile──> ExecutionPlan
      解析端口签名（channels: "param:xxx" 表达式、count 可变端口展开）
      → 校验（方向/类型/格式/通道数/采样率/输入唯一驱动）→ 拓扑排序 → 每连接一个 Buffer
  ──plan.json──> 宿主执行
```

## C ABI 要点（orpheus_abi.h）

- 组件导出唯一入口：`ORPHEUS_ENTRY_NAME` 宏包裹的函数（缺省名 `orpheus_get_interface`），返回 `OrpheusComponentInterface*` 函数表：create/destroy/prepare/reset/process/set_parameter/get_parameter/get_state_value。
- `OrpheusProcessContext`：`inputs[]/outputs[]` + `input_count/output_count` + `frame_count` + `sample_rate`。槽位 = manifest 端口声明顺序（可变端口按 `count` 展开为 `<id>0..N-1`）；**未连接引脚为 NULL，必须判空**。
- `OrpheusConfig`（prepare/create 传入）：sample_rate/block_size/channels + 参数表（param_ids/param_values）。**组件必须在 prepare 读取初始参数**（曾有 gain_db 不读的 bug）。
- Buffer：f32 交错布局，frame_capacity 帧。
- ABI 禁止跨边界传 C++ 对象/STL。

## Runtime 宿主分工

| 宿主 | 时钟 | 用途 |
|---|---|---|
| `orpheus_runtime.exe <plan> <component_dir>` | 自由跑（越快越好） | 纯文件图离线处理；跑完打印 `PROBE` 回读行 |
| `orpheus_rt_host.exe <plan> <component_dir> [sr] [bs]` | 声卡回调 | 含 device_in/device_out 的图；stdin 控制协议 |

rt_host 设备拓扑按图内容：in+out 双默认设备=duplex；in+out 指定设备或 loopback=异步桥（capture→ma_pcm_rb→playback 主时钟，带欠载/溢出侦测）；仅 out=播放时钟；仅 in=采集/环回时钟。回调按 block_size 分块（设备周期可能更大，不分块会溢出崩溃）。

## 后端 API 面（orpheus_core/server/app.py，前缀 /api）

- `GET /components`（含 manifest 端口/参数/widget 元数据）、`POST /components/rescan`
- `GET/POST /projects`、`GET/PUT/DELETE /projects/{name}`、`GET /projects/{name}/download`（zip）
- `POST /projects/{name}/compile`、`POST .../run`（按图分流 offline/realtime）、`POST .../run_generated`
- `POST .../rt/start|stop`、`GET .../rt/status`、`POST .../rt/param`
- `GET /projects/{name}/files[?ext=]`、`GET .../files/{relpath}`、`POST .../uploads`
- `GET /devices`（rt_host --list-devices，30s 缓存）

工程存 `workspace/<name>/project.yaml`（写穿）；WAV 路径相对工程目录（运行子进程 cwd=工程目录）。

## 前端结构（ui/src/）

- `App.js`：主控（多视图标签页、工程管理、保存/编译/运行、实时会话轮询、包装子组件）
- `graphUtils.js`：文档↔画布双向转换、`resolvePorts`（可变引脚展开）、子组件目录合并
- `widgets.js`：参数控件注册表（number/text/slider/select/checkbox/file），manifest `widget` 字段选择
- `nodeWidgets.js`：节点本体定制注册表（按组件 id，如探针电平条）
- `Palette.js`（分类树）、`ParamPanel.js`（通用参数置顶分组）、`SubPortsPanel.js`、`FileBrowseModal.js`
