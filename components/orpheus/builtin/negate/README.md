# 变号(取反相) 组件说明
> 组件 ID `orpheus.builtin.negate` · 类型：基础/增益与混音，`out = -in`
> 用于主动降噪等需要“取反相抵消信号”的场景。

## 1. 原理
对每个样本每个通道做 `out = -in`：幅度不变、位相反转 180°。在 ANC 中，把自适应滤波器计算出的抵消信号 `y` 取反，就得到发给扬声器的反相信号 `-y`，与噪声 `d` 叠加相消。

## 2. 端口与参数
- 端口：`in` / `out`，通道数 `channels`（默认1，restart生效）。
- 开销与 inplace：`latency_samples=0`、`supports_inplace=true`、`realtime_safe=true`。

## 3. 用法（怎么接）
把它接在需要取反相的信号后，例如在分解版 FxLMS 链路中：
`adaptive_fir:out → negate:in`，`negate:out → 扬声器`；这样扬声器播放的就是反相抵消信号 `-y`。

## 4. 实例（对应 `examples/anc_fxlms_decomposed.yaml`）
在分解版主动降噪工程中，`negate`(若干 `neg`) 负责取反：
- `adaptive_fir:out → neg:in`，`neg:out → 扬声器(wav_out)`。
- 详见分解版链路说明 `components/orpheus/builtin/adaptive_fir/README.md` 与 `docs/HOW.md` §32。
