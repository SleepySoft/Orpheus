# 双 Runtime 模型蒸馏底稿

本目录是对应 Orpheus 工程和蒸馏索引的共同根目录。`project.yaml` 只保存早期结构投影，不代表完整算法已经蒸馏；真实进度以索引门禁为准。

## 从哪里开始

1. `algorithm-index.yaml`：全量 Algorithm IR；67 个递归算法定义、11 个任务、2 个 typed bus、端口/状态/操作/阻塞项和系统关系。typed bus 不属于算法组件。
2. `distillation-progress.yaml`：25 个一级模块的六项验证进度，不承担结构描述。
3. `distillation-workflow.md`：单模块从源码证据到 golden、再到 Orpheus 子图的固定步骤。
4. `analysis/`：按模块记录用途、为何这样处理、公式/状态与未决问题。
5. `evidence/`：提取器生成的机器可读参数、shape、hash 和源指纹。
6. `project.yaml`：旧结构投影，仅用于历史对照；完整研究图见 `../../external_model_full/`。

## 当前已提取

| 模块 | 分析 | 参数证据 | 仍缺 |
|---|---|---|---|
| InputSelect / InputMixer | `analysis/input-routing.md` | `evidence/input-routing-defaults.json` | RTC 事务、golden |
| Volume / Tone / Balance | `analysis/big6.md` | `evidence/big6-defaults.json` | tone 状态方程、freeze/reset、golden |
| PreAmp 路由/电平 | `analysis/preamp-routing.md` | `evidence/preamp-routing-defaults.json` | InnerLink 状态机、golden |
| PostProcess | `analysis/postprocess.md` | `evidence/postprocess-defaults.json` | OutputCal 初值、RTC/reset、golden |

尚未通过六项门禁的模块不得进入完整系统图。局部 HEQ 图也只是研究切片，不能替代 Runtime B 的并行 IIR、运行期更新和后空间处理。

运行 `python -m orpheus_core.cli index-generate algorithm-index.yaml ../../external_model_full` 可确定性重建完整研究图。生成图保留全部未实现算法为 placeholder，因此可浏览但不可误编译。

本地拥有对应生成输出时，可设置 `ORPHEUS_EXTERNAL_MODEL_A_ROOT` 为 Runtime A 生成目录；索引测试会重新运行四个提取器，并与已提交 evidence 做字节级比较。仓库不保存个人绝对路径。
