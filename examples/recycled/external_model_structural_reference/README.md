# 双 Runtime 模型蒸馏底稿

本目录是对应 Orpheus 工程和蒸馏索引的共同根目录。`project.yaml` 只保存早期结构投影，不代表完整算法已经蒸馏；真实进度以索引门禁为准。

## 从哪里开始

1. `distillation-index.yaml`：25 个模块的边界、依赖、证据、进度和完整图门禁。
2. `distillation-workflow.md`：单模块从源码证据到 golden、再到 Orpheus 子图的固定步骤。
3. `analysis/`：按模块记录用途、为何这样处理、公式/状态与未决问题。
4. `evidence/`：提取器生成的机器可读参数、shape、hash 和源指纹。
5. `project.yaml`、`notes.md`、`node-notes.json`：归档结构图及其说明，只用于理解系统边界。

## 当前已提取

| 模块 | 分析 | 参数证据 | 仍缺 |
|---|---|---|---|
| InputSelect / InputMixer | `analysis/input-routing.md` | `evidence/input-routing-defaults.json` | RTC 事务、golden |
| Volume / Tone / Balance | `analysis/big6.md` | `evidence/big6-defaults.json` | tone 状态方程、freeze/reset、golden |
| PreAmp 路由/电平 | `analysis/preamp-routing.md` | `evidence/preamp-routing-defaults.json` | InnerLink 状态机、golden |
| PostProcess | `analysis/postprocess.md` | `evidence/postprocess-defaults.json` | OutputCal 初值、RTC/reset、golden |

尚未通过六项门禁的模块不得进入完整系统图。局部 HEQ 图也只是研究切片，不能替代 Runtime B 的并行 IIR、运行期更新和后空间处理。

本地拥有对应生成输出时，可设置 `ORPHEUS_EXTERNAL_MODEL_A_ROOT` 为 Runtime A 生成目录；索引测试会重新运行四个提取器，并与已提交 evidence 做字节级比较。仓库不保存个人绝对路径。
