# rfft - 实数FFT

> Orpheus 专用扩展组件（L1），对应 BAF SAS 的 FDP / Audiopilot 频域分析 RFFT 块。

## 功能

实数快速傅里叶变换。将 N 点实数时域信号变换为半复数（half-complex）格式频域信号。FFT 点数等于图块长（block_size），需配合 `downrate` 上游组件获取大块（如 32 样本块 ×8 下采样因子 = 256 点 FFT）。

是 FDP 频域环绕解码器与 Audiopilot 自适应分析的核心频域变换原语。

## 端口

| 端口 | 方向 | 类型 | 通道 |
|------|------|------|------|
| `in` | 输入 | audio f32 | param:channels |
| `out` | 输出 | audio f32 | param:channels |

## 参数

| 参数 | 类型 | 默认 | 范围 | 更新策略 | 说明 |
|------|------|------|------|----------|------|
| `channels` | int | 2 | [1, 32] | restart | 通道数（影响签名） |

> FFT 点数无独立参数，固定 = `block_size`（须为 2 的幂，4~1024）。

## BULK 槽

无 BULK 槽。twiddle 因子在 `prepare` 阶段预计算到状态结构体。

## 算法

radix-2 迭代 FFT（就地位反转 + 蝶形），每通道独立处理：

```
prepare:
  预计算 twiddle: W[k] = exp(-2*pi*i*k/N), k=0..N/2-1

process (每通道 c):
  复制实数输入 -> scratchR[], 虚部 scratchI[] 清零
  fft_forward(scratchR, scratchI, N, cosT, sinT)
  打包半复数格式:
    hc[0]         = R(0)        // DC
    hc[1..N/2-1]  = R(k)
    hc[N/2]       = R(N/2)      // Nyquist
    hc[N/2+1..N-1]= I(N-k)      // 虚部逆序
```

- 半复数格式：N 个 float 承载 N/2+1 个复数 bin（利用实数 FFT 的 Hermitian 对称性）
- 输出帧数 = N（与输入块长一致）
- 与 `ifft` 配对可完美重构（round-trip identity）

## 使用示例

```yaml
# 256 点实数 FFT：先 downrate 8x 把 32 样本块汇聚为 256
- id: down
  component: orpheus.builtin.downrate
  params: {factor: 8, channels: 2}
- id: fft
  component: orpheus.builtin.rfft
  params: {channels: 2}
```

## 源码映射

| BAF SAS 源码 | 本组件 |
|---|---|
| `Model_1_1.c:10182-10900` FDP `rfft_process_inplace`（256 点，2ch） | `rfft` 256 点半复数输出 |
| FDP 一次复 FFT 算两路实 FFT（SHARC+ cfftf 优化） | 逐通道独立 radix-2（未做双路打包优化） |
| Audiopilot35 频域分析（256 点） | 同一 `rfft` 组件复用 |

## 限制

- 仅 radix-2（点数须 2 的幂），最大 1024 点
- FFT 点数 = block_size，无独立 fft_size 参数（受编译器 `_resolve_value` 仅支持 `*`/`/` 限制）
- 未实现 FDP 的「双路实序列打包成一路复 FFT」优化（功能等价，性能不同）
