# NLMS残差分析

`orpheus.builtin.noise_detector_nlms` 用逐通道 NLMS 学习 `ref → in` 的线性路径。
残差既可能含噪声/失真，也可能来自模型失配、未收敛或时延超出滤波器长度，
**不是标准 THD+N**，不能仅凭指标判断被测信号是否干净。

## 接口与真实调用

- `ref` 仅提供参考数据，`process: false`；上游必须先准备参考，再交付 `in`。
- `in` 是唯一驱动入口，每次正帧调用都处理；不等会合、不合并同拍调用。
- `out` 的驱动编号为 1，统计提交后直通被测信号并显式 emit。
  下游失败不回滚已处理的权重、历史和统计。
- `out` 可不连接，此时仍完整分析、不伪造交付。连接时不支持原地或部分重叠。
- 输入必须覆盖本次 `ctx.frame_count`；这是流式时域算法，不要求 FFT 窗口或固定批次。
  零帧不推进；非法入口、缺失/短输入、容量和私有资源别名明确报错。

## 参数与数学

`channels` 默认 2（1～32），`filter_length` 默认 64（1～4096）；
`step_size` 默认 0.1，`leakage` 默认 1，均在 0～1；
`eps` 默认 1e-6（1e-12～1），`time_thres` 默认 0.02（0～1），
`frame_thres_db` 默认 -25 dB（-120～0）。设置均需重启。

逐通道、逐样本先以旧权重计算 `y = Σ w[k] * ref_history[k]`，
再取内部误差 `e = in - y`，更新：

```text
w[k] = leakage*w[k] + (step_size*e/(Σ ref_history[k]² + eps))*ref_history[k]
```

预测、归一化和权重更新保留原 f32 次序；
误差/输入能量先转 double 再相乘、累计。
这与使用外部误差输入的 `nlms` 组件不同。

## 观测

- `residue_db`：当前真实调用 `10*log10((Σe²+1e-12)/(Σin²+1e-12))`。
- `erle_db`：reset 以来累计输入/残差能量比，保留原 f32 `log10f` 计算。
- `noise_frames`：调用级残差比严格大于 `frame_thres_db` 的次数，
  判定使用未转 f32 的 double dB；不是样本数，也不表示 Task 拍数。
- `noise_ratio`：噪声调用数/总正帧调用数。
- `clicks`：`abs(e)>0.2` 的累计样本数，不是相邻样本差分。
- `residue_pk`：reset 以来最大绝对残差。
- `detail`：非实时 getter 生成 JSON，包含当前 dB/ERLE、噪声/总调用数、
  `abs(e)>time_thres` 的样本数、总样本数、突刺和累计峰值。
  调用/样本计数为 64 位；两个 int32 计数探针饱和，JSON 保留完整数值。
  溢出/不变量错误在推进前拒绝，非有限 JSON 指标或格式化失败明确报错。

延续原公式：输入与残差都为零时微量比为 1，`residue_db=0`，
仍可能超过负 dB 预算；不将这种结果解释为实际检测到了噪声。
变长调用按实际样本累计能量，不由最后块长反推。
reset 清权重、历史、位置及所有统计/探针（包括占比），保留配置。

## 独立生成与验证

先保存 `component.yaml`，再由 `scripts/generate_component.py` 独立校验/生成。
用户算法仅在 `user/`，生成文件不作为手写入口。
原生冒烟以非环形参考逐位核对权重和指标，覆盖三次分配失败重试、
3/1/2 帧实际调用、错误后继续、容量/别名/入口、计数边界、纯观测和 reset；
`test_noise_detector_nlms_interfaces.py` 覆盖五图的动态/生成四入口和最新 JSON 读回。
