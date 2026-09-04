# BAF 生成模型对齐记录

## 参考代码

ASM：

```text
C:\D\Work\Project\EREV\cart-cicd-erev-asm\components\baf\src\out\baremetalgul
```

EREV-1 SAS：

```text
C:\D\Work\Project\EREV\cart-cicd-erev-1\components\baf\src\out\baremetalgxp
```

这些目录是本地验证输入，不作为 Orpheus 构建依赖，也不复制其生成源码或完整参数表。

## ASM 调度实证

`baremetalgul/src/Baf.exec_graph.json`：

| TID | callrate | 周期 | Orpheus 块长（48 kHz） |
|---|---:|---:|---:|
| 0 | 1 | 0.1667 ms | 8 |
| 1（base） | 3 | 0.5 ms | 24 |
| 2 | 4 | 0.6667 ms | 32 |
| 3 | 24 | 4 ms | 192 |
| 4 | 30 | 5 ms | 240 |
| 5 | 192 | 32 ms | 1536 |
| 6 | 768 | 128 ms | 6144 |

`symphony_asm_ehc_rnc.yaml` 的全部跨 Task 边现已通过 `async_bridge` 显式连接。

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
python scripts/extract_baf_top.py `
  <Model_Target_Rnc_p15_b5_TOP.c> NlmsAdaptiveFilterCoeffsInit `
  --expect-count 12000 --format csv --output outputs/rnc_initial_weights.csv
```

## SAS SoftClipper

生成证据：EREV-1 `rt_sys_PostProcess_87.c` 的 `Model_1_1_MATLABFunction`：

$$
x_1=\min(|u|,x_{max}),\quad x_2=\max(x_1-x_{min},0),\quad
y=\operatorname{sign}(u)(x_1-p_2x_2^2)
$$

`Model_1_1_PostProcess_p0_b0_TOP.c` 默认：

- `xmin=0.65`
- `xmax=1.35`
- `p2=0.714285731`
- high/low 两档相同

Orpheus 组件：`orpheus.builtin.baf_soft_clipper`。`symphony_sas_step0.yaml` 已替换原 tanh 占位。

## 当前验证

- RNC MIMO NLMS：非零初始权值卷积 golden + 两帧归一化更新 golden。
- BAF SoftClipper：阈值以下、二次曲线、上限饱和、负号与 active mask golden。
- ASM/SAS 示例均可编译为 plan。
- ASM 独立生成工程 48 targets、SAS 独立生成工程 68 targets 构建成功。
- 两个生成程序均完成少量图块运行；ASM 全局入口及 TID1/TID5/TID6 入口返回 0。
- ASM 工程附带教学包：编译、MIMO NLMS 组件/维度、跨子图控制链与异步桥数量均可在 UI 一键检查。

## 后续模型优先级

1. RNC filtered-error：6 roof mic × 8 speaker × 200 taps 与 8×8 speaker × 200 taps Wiener FIR。
2. RNC 系数历史/发散恢复：`NlmsAlphaControlCoeff=0.998849392` 及双阶段检测状态机。
3. EHC Core：谐波参考生成、FxLMS 和 896 项 HarmFreqTable。
4. SAS FDP：TID0/TID2 双速率 256 点 STFT、50% overlap 与 6 路解码。
