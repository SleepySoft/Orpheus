# 外部参考模型 生成模型对齐记录

## 参考代码

ASM：

```text
外部参考工程 A 的生成代码目录
```

参考工程 B SAS：

```text
外部参考工程 B 的生成代码目录
```

这些目录是本地验证输入，不作为 Orpheus 构建依赖，也不复制其生成源码或完整参数表。

## ASM 调度实证

`baremetalgul/src/model-a-exec-graph.json`：

| TID | callrate | 周期 | Orpheus 块长（48 kHz） |
|---|---:|---:|---:|
| 0 | 1 | 0.1667 ms | 8 |
| 1（base） | 3 | 0.5 ms | 24 |
| 2 | 4 | 0.6667 ms | 32 |
| 3 | 24 | 4 ms | 192 |
| 4 | 30 | 5 ms | 240 |
| 5 | 192 | 32 ms | 1536 |
| 6 | 768 | 128 ms | 6144 |

`example-a.yaml` 的全部跨 Task 边现已通过 `async_bridge` 显式连接。

## RNC MIMO NLMS

生成证据：

- `Model_Target_TOP.h`：`NlmsAdaptiveFilterCoeffsInit[12000]`。
- `RncSub.c` `<S724>/AdaptFilter`：12 个 active accelerometer、8 个 speaker、125 taps。
- 权值索引：`1500 * speaker + 125 * reference + tap`。
- norm：全部 12×125 reference history 的能量和，再加 `1e-5`。
- 每输出更新量：`StepSizeGain * NlmsStepSize[m] * filtered_error[m] / norm`。
- leakage 每块只处理一条 reference/output FIR，轮转摊销 CPU。

Orpheus 组件：`orpheus.builtin.rnc_mimo_nlms`。

当前 `Model_Target_Rnc_p15_b2_TOP.c` 的 `NlmsStepSize[8]` 全为 0。b5 初始权值经提取器验证：

```text
count: 12000
sha256: 0e908d3303747c0b7f03332f8961dfd66903f29653184d0a63902fec38fdf725
first: 0.00640417775, 0.00927445106, 0.011966506, 0.014007655
last: 0.000267774099, 0.0000336508019, -0.0000579309, -0.0000534932
```

提取命令：

```powershell
python scripts/extract_external_model_top.py `
  <Model_Target_Rnc_p15_b5_TOP.c> NlmsAdaptiveFilterCoeffsInit `
  --expect-count 12000 --format csv --output outputs/rnc_initial_weights.csv
```

## SAS SoftClipper

生成证据：参考工程 B `rt_sys_PostProcess_87.c` 的 `Model_1_1_MATLABFunction`：

$$
x_1=\min(|u|,x_{max}),\quad x_2=\max(x_1-x_{min},0),\quad
y=\operatorname{sign}(u)(x_1-p_2x_2^2)
$$

`Model_1_1_PostProcess_p0_b0_TOP.c` 默认：

- `xmin=0.65`
- `xmax=1.35`
- `p2=0.714285731`
- high/low 两档相同

Orpheus 组件：`orpheus.builtin.baf_soft_clipper`。`examples/symphony_baf_structural_reference.yaml` 使用生成代码的二次分段实现，不再使用 tanh 近似。

## BAF 结构参考

`examples/symphony_baf_structural_reference.yaml` 以 EREV BAF 1.0.3 的生成输出为调查边界，合并了此前分散的 SAS step0、PostProcess 和组件验证模型：

- Baf1 / `Model_1_1`：6 个 TID、全速率 SAS、PostProcess 与 Audiopilot；
- Baf2 / `Model_1_2`：5 个 TID、Deci FDP/Mixing、Peripheral/Headrest/Overhead EQ 与 Deci PostHoligram；
- `BVP_Config.yml`：Baf1->Baf2 的 488 元素总线和 Baf2->Baf1 的 1193 元素总线；
- BVP 数组字段的数量和方向记录。

该工程明确标记为 `structural_reference`：数组控制总线只做结构记录，1/2/7/22 路块数据使用显式音频端口，FDP、Audiopilot、Medusa HEQ/VLS 和多 Task 调度尚未数值对齐。字段、周期和缺口见同名 `.notes.md` 与 `model_tree.fidelity`。

## Model_1_2 Headrest / Overhead HEQ 可执行切片

`examples/symphony_baf_heq/` 是目录工程机制落地后的第一个真实算法蒸馏切片：

- `p10_b1.MedusaHeadrestCompEqFirCoeffsTarget[21200]`：40 个 530-tap filter；每个默认仅 tap 529 为 1.0。
- `p10_b0.MedusaHeadrestCompEqFirInputMapping[40]`：10 路输入按 `0..9` 重复四次；OutputMapping 为 `0..39`，输出 40 路。
- `p9_b1.MedusaOverheadHeqFirCoeffsTarget[10600]`：20 个 530-tap filter；每个默认仅 tap 529 为 0.2。
- `p9_b0.MedusaOverheadHeqFirInputMapping[20]`：`[0,2,4,6,8,1,3,5,7,9,10,12,14,16,18,11,13,15,17,19]`。
- Overhead OutputMapping `[0,5,10,15]` 把每五个 filter 求和为一路；两条链均保留生成代码的两样本跨块 carry。

通用组件 `orpheus.builtin.mapped_fir_bank` 实现输入映射、系数集映射、变长 FIR、输出分组求和和统一输出延迟。系数由 `scripts/extract_baf_heq.py` 从 TOP 生成 `f32le` 资源并以 shape/hash 校验：

- Headrest FIR SHA-256：`2310cfbe35d2fd693a702dc1b9769232146f64670711a923765f94c0c3b13f9d`
- Overhead FIR SHA-256：`c0e4d8b1f7d5f24f7426f1c6613489a25b3ba2a2af12f0825071f06e0bcf572a`

当前 p9/p10 PoolIIR TOP 系数全零，因此默认输出的有效算法部分是 FIR 主支路。运行期 room-mode IIR 系数更新、enable/bypass 仍是该子系统后续阶段，不能据此宣称所有调音状态等价。

## 当前验证

- RNC MIMO NLMS：非零初始权值卷积 golden + 两帧归一化更新 golden。
- 外部参考模型 SoftClipper：阈值以下、二次曲线、上限饱和、负号与 active mask golden。
- ASM 与 BAF 结构参考均可编译为 plan。
- BAF 结构 plan 展开为 114 个原子节点，其中 Baf2 为 16 个。
- 123 个可见节点均有工程注释；测试强制 fidelity 声明和 100% 注释覆盖。
- HEQ 目录工程通过 import/resource/flatten/compile/codegen 测试，资源可从外部 TOP 字节级重复生成。
- 两个生成程序均完成少量图块运行；ASM 全局入口及 TID1/TID5/TID6 入口返回 0。
- ASM 工程附带教学包：编译、MIMO NLMS 组件/维度、跨子图控制链与异步桥数量均可在 UI 一键检查。

## 后续模型优先级

1. RNC filtered-error：6 roof mic × 8 speaker × 200 taps 与 8×8 speaker × 200 taps Wiener FIR。
2. RNC 系数历史/发散恢复：`NlmsAlphaControlCoeff=0.998849392` 及双阶段检测状态机。
3. EHC Core：谐波参考生成、FxLMS 和 896 项 HarmFreqTable。
4. SAS FDP：TID0/TID2 双速率 256 点 STFT、50% overlap 与 6 路解码。
