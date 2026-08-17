# ifft - 实数IFFT

> Orpheus 专用扩展组件（L1），对应 Symphony SAS 的 FDP `rifft` 块（overlap-add 重建）。

## 功能

实数逆快速傅里叶变换。将半复数（half-complex）格式频域信号还原为 N 点实数时域信号。与 `rfft` 严格配对，FFT 点数等于图块长。

是 FDP 频域环绕解码器的重建原语（IFFT 后接 overlap-add 完美重构）。

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

无 BULK 槽。twiddle 因子在 `prepare` 阶段预计算。

## 算法

利用 `IFFT(X) = (1/N) * conj(FFT(conj(X)))` 公式，复用正向 FFT 核：

```
process (每通道 c):
  解包半复数 -> 完整复数（Hermitian 对称）:
    R(0)=hc[0], I(0)=0
    R(k)=hc[k], I(k)=hc[N-k]          for k=1..N/2-1
    R(N/2)=hc[N/2], I(N/2)=0
    R(N-k)=R(k), I(N-k)=-I(k)          // Hermitian
  conj(X): 虚部取反
  fft_forward(scratchR, scratchI, N, cosT, sinT)
  conj(result)/N: 虚部取反并缩放 1/N
  输出实部 scratchR[]
```

- 输入须为 `rfft` 的半复数输出格式
- 输出帧数 = N
- `rfft -> ifft` 为恒等变换（数值精度内）

## 使用示例

```yaml
# rfft -> ifft 完美重构（round-trip 恒等）
- id: fft
  component: orpheus.builtin.rfft
  params: {channels: 2}
- id: ifft
  component: orpheus.builtin.ifft
  params: {channels: 2}
```

## 源码映射

| Symphony SAS 源码 | 本组件 |
|---|---|
| `Model_1_1.c` FDP `rifft`（256 点，6ch） | `ifft` 256 点半复数输入 |
| FDP IFFT + OverlapAdd（InputOverlap 叠加） | `ifft` 仅做逆变换；overlap-add 由下游 `delay`+`mixer` 组合实现 |
| conj-FFT 逆变换公式 | `conj(FFT(conj(X)))/N` |

## 限制

- 仅 radix-2（点数须 2 的幂），最大 1024 点
- 不含 overlap-add（需配合 `delay` + `mixer` 在图中搭建）
- FFT 点数 = block_size，无独立 fft_size 参数
