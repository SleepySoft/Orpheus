# orpheus.builtin.probe_rms — RMS电平表

## 功能

最简单的 RMS 监控组件：把输入音频原样直通，同时计算每个 process 块内所有通道/样本的 RMS 值，通过 `rms` 探针上报。适合 UI 电平表、自动化测试断言、或监控链路某一点的信号能量。

`component.yaml` 的 `interfaces` 是声明事实源，独立脚本生成
`generated/`，算法位于 `user/algorithm.c`。
每次非空 `in` 交付按实际帧数计算所有通道的合并 RMS，先更新只读 `rms`，
再显式发射出口 1；支持短交付、原地及重叠搬运，直通保留原始位型。
同一 Task 内多次到达分别测量和调用，不等待、不去重。
零处理量子不写数据、不发射，保留上次读数；prepare/reset 清零。
`rms` 保留 `control_source` 能力；Task 链中的参数控制按目标每次调用前交付，
不会人为延迟一块或只更新一次。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels` | int | 2 | 通道数，改变后需重新编译。 |
| `rms` | float | 0.0 | 当前块 RMS（readback，只读）。 |

### `rms` 的真实含义

计算公式：

```
rms = sqrt(sum(x^2) / (frames * channels))
```

它反映的是当前块内信号的“有效值”。

- 纯正弦波幅度为 A 时，rms ≈ A / sqrt(2) ≈ 0.707A。
- 满幅方波 rms = 1.0。
- 静音 rms = 0。

## 端口

- `in`: 输入音频，`channels` 通道。
- `out`: 输出音频，与输入完全一致。

## 注意事项

- 该组件没有平滑/释放逻辑，`rms` 每块独立更新，波动会比较明显。如果需要“电平表”般的稳定读数，请用 `level_detect`（mode=1）。
- 实时安全，计算量极小。
- 组件使用 ABI v7，探针元数据和注册由接口生成壳维护，外部不能写入 `rms`。

## 典型用法

```
main_out ──► probe_rms ──► wav_out
```

把 `probe_rms` 放在最终输出前，即可在 UI 实时观察输出 RMS，同时不影响 WAV 写入。
