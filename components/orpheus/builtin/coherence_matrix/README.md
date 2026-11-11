# orpheus.builtin.coherence_matrix — 相干矩阵

## 功能

对多路输入信号逐块做 FFT，并估计通道间的平均相干（magnitude-squared coherence），输出一个 `channels × channels` 相干矩阵，以及非对角元素的平均历史曲线。音频本身原样直通，不改变声音。

常用于分析麦克风阵列、扬声器阵列或 Audiopilot 等算法中“哪些通道在同步发声”。例如 10 路麦克风同时拾取同一声源时，相干矩阵的非对角元素会接近 1；如果各路噪声独立，则接近 0。

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels` | int | 2 | 输入/输出通道数，范围 2~10。改变后需停止并重建执行图。 |
| `fft_size` | int | 128 | 显式载荷点数，4~4096的2次幂；改变后需停止并重建执行图。与Task量子无关。 |
| `smoothing` | int | 8 | 指数平滑的等效块数。越大曲线越平滑、响应越慢。 |
| `coherence` | string | "{}" | 当前相干矩阵 JSON（readback，只读）。 |
| `history` | string | "[]" | 非对角平均相干历史（readback，只读）。 |

### `smoothing` 的真实含义

内部使用一阶指数滑动平均（EMA）：

```
new_value = old_value + alpha * (instant - old_value)
alpha = 1 / smoothing
```

- `smoothing = 1`：最快响应，但波动大。
- `smoothing = 32`：非常平滑，但突变会被拖长。

### `coherence` JSON 结构

返回格式示例：

```json
{"n":4,"bins":16,"matrix":[1,0.9,0.8,0.1,0.9,1,...]}
```

`matrix` 按行优先排列，共 `n × n` 个元素。保留前 `fft_size/2` 个频点，
不计Nyquist；每个分母不大于1e-12的频点计0，再求平均并钳位上限1。
因此静音通道及未积累能量时的对角线为0，不保证所有对角线恒为1。

## 端口

- `in`: 唯一驱动入口，每次交付完整 `fft_size × channels` 载荷。
- `out`: 音频原样直通，显式 `emit out(1)`。

## 注意事项

- 输入、输出接口明确声明 `block_size: param:fft_size`；必须接相同载荷规格。
  不再从宿主Task块长推断FFT点数，也不对错误载荷静默直通。
  例如Task量子8、FFT载荷4096是合法组合，交付仍保留Task原有时间锚点与有效帧元数据，
  不把批次点数伪装成Task时间线长度。
- 内部使用基 2 FFT，复杂度为 `O(channels × N log N)`，不适合在超小block上高频调用。
- 权重谱、矩阵和最多64次真实调用的历史先提交，再交付音频；
  同拍重复调用分别处理，下游失败不回滚。零帧不推进、不交付。
- 完整原地允许，部分重叠与私有状态别名拒绝；reset清功率谱、矩阵和历史，保留配置。
- process不分配、不格式化JSON；五组缓冲在prepare分配，失败统一清理并可重试。
  `coherence` / `history` 仅在非实时getter中按最新数值格式化，
  名称、ID与生成探针枚举一致。非有限值与容量/格式化错误明确报错，不静默截断。
  prepare/reset后矩阵读回为零矩阵，历史为 `[]`。

## 典型用法

```
mic_array (10ch) ──► coherence_matrix ──► null_sink
```

把麦克风阵列信号接入 `coherence_matrix`，然后在 UI 或 RT host 中读取 `coherence` 探针，即可观察各路信号的相关性，用于判断噪声源方向或音乐/噪声分离效果。

原工程需显式配置FFT载荷并按真实来源顺序整体迁移，不能依赖旧Task块长隐式绑定。

## 验证

- `orpheus_core/tests/test_coherence_interfaces.py`：4图×四入口，
  冲激的解析频谱与独立f32 EMA预期验证音频逐位直通、最新矩阵/历史JSON、
  10通道/4096点边界及Task量子与载荷分离。
- `tests/coherence_matrix_smoke.c`：独立double DFT对照功率/交叉谱/相干，
  谱误差小于 `1e-5*(1+abs(reference))`、矩阵/历史误差小于1e-5；
  68次交付验证64项历史滚动、下游错误后继续、实时无格式化、
  五次分配失败清理重试、原地、非法输入/别名/配置、零帧、reset与非有限JSON错误。
