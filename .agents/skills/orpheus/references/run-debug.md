# 运行与调试参考

## 目录

- 运行入口与分流
- 实时会话控制协议
- 日志约定（组件如何输出到 UI 日志窗口）
- 故障排查目录（症状→原因→修法）

## 运行入口与分流

- `POST /api/projects/{name}/run`：含 device_in/device_out → 实时会话（rt_host 子进程）；否则离线宿主跑完即退出。响应 `mode: realtime|offline`。
- `POST /api/projects/{name}/run_generated`：代码生成路径（生成 C 工程→静态构建→运行，块数按输入 WAV 长度计算）。
- UI：「▶ 运行」= 上面的 /run；「⚙ 编译后运行」= run_generated；实时会话出现时出现「■ 停止」。

## 实时会话控制协议（rt_host stdin/stdout 文本行）

- stdin：`SET <node> <param> <value>`（纯数字→FLOAT，否则→STRING）/ `GET <node> <param>` / `STOP`（回车或 stdin EOF 同效）
- stdout：`LOG ...`（生命周期/警告）、`PROBE <node> <param> <value>`（每 200ms 上报 readback 参数）、`OK/ERR SET|GET ...` 回显
- 后端 `RtSession` 读线程解析进日志环形缓冲（500 行）与 probes 字典；UI 每秒轮询 `rt/status`
- UI 运行中改参数：非 `restart_required` 的参数即时 `rt/param` 推送；restart_required 的提示需重启会话

## 日志约定

- 主机事件：`LOG ...` 行（启动参数、设备协商周期、停止）
- 组件日志：只允许在 create/prepare/destroy/set_parameter 里 printf（stdout 被捕获进 UI 日志窗口）；**process 里禁止任何输出**（实时线程）
- 运行中的组件数据输出：用 readback 参数 + PROBE 轮询，不要用 log
- 水位警告：`LOG WARN 播放欠载 xN/s`（建议增大 block_size）/ `LOG WARN 采集溢出 xN/s`（时钟漂移，建议共用设备）

## 故障排查目录

| 症状 | 原因 | 修法 |
|---|---|---|
| `LoadLibrary error=126` | DLL 文件名缺 `lib` 前缀或未用绝对路径 | loader 用 `lib<target>.dll` 绝对路径 |
| cmake configure 失败/crt2.o 找不到 | PATH 解析到 Perl 的 gcc 4.9.2 | 用主构建 CMakeCache 的 CMAKE_C_COMPILER（Strawberry） |
| 生成工程一跑就崩 | 多组件静态链接共用 `orpheus_get_interface` 符号 | 组件入口用 ORPHEUS_ENTRY_NAME 宏（已全量修复，新组件照抄） |
| 生成工程行为不对（参数无效） | 旧生成器不传参数 | 已修复：生成类型化参数表传入 prepare |
| 实时一停就崩 (0xC0000005) | 设备回调周期 > block_size 导致缓冲溢出 | 回调内按 block_size 分块（已实现）；勿回退 |
| 连续哒哒声 | 设备周期太小（如 2.7ms）欠载 | 已实现 10ms 周期下限；仍有则看 LOG WARN |
| 每隔几秒咔一声 | 采集/播放设备时钟漂移 | 走异步桥（指定设备或 loopback 即自动启用） |
| 设备名/日志中文乱码 | 子进程管道按 GBK 解码 UTF-8 | subprocess 加 encoding="utf-8"（已全部修复） |
| 子进程日志块状延迟 | MinGW 全缓冲 + Python 管道迭代预读 | 子进程 setvbuf(_IONBF)+unitbuf；Python 用 readline() |
| 调 gain 音乐不变 | 音乐没进图（device_in 在采集麦克风） | device_in 采集源改 Loopback，或用 VB-Cable 路由 |
| 自激啸叫 | 麦克风→扬声器声学反馈 | 用耳机/降增益；或改 loopback 采集 |
| 运行失败无原因 | 进程死在被轮询发现前 | 已实现：退出时自动展开日志面板显示 exit code + 末尾日志 |
| 组件编译了但 scan 找不到 | manifest 校验失败（Registry 打印 skip 原因） | 看扫描输出，按 schema 修正 component.yaml |
| UI 改动不生效 | 忘了重新 `npm run build`（serve 托管的是 build 产物） | 重新 build；或开发时用 `npm start`（:3000 热更新） |
| PowerShell 改源码后中文坏了 | PS 5.1 默认 GBK 读写 | 用 UTF-8 感知工具编辑；`git checkout -- <file>` 恢复 |
