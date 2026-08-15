# orpheus.builtin.fir — FIR 滤波器

## 功能

通用有限冲激响应（FIR）滤波器：从 `coefficients` 字符串解析出一串系数，按环形延迟线做逐样本卷积 `y[n] = Σₖ coeffs[k] · x[n−k]`。系数个数即阶数（taps），上限 1024。

FIR vs IIR 一句话取舍：**FIR 系数直接就是冲激响应，可做到严格线性相位（对称系数）且永远稳定，代价是同样的陡峭度需要比 IIR 多得多的阶数和算力**；IIR（如 `biquad`）用很少的系数就能做出陡峭响应，但有相位失真、且系数设计不当可能不稳定。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | 输入音频，`channels` 通道 |
| out | output | audio | 卷积后的音频，通道数与输入相同 |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `coefficients` | string | "1.0" | 系数串（BULK 文本），逗号/空格/制表符分隔的浮点数；restart_required |
| `channels` | int | 2 | 通道数，1~32；改变后需重新编译（affects_signature） |
| `taps` | int | 0 | 实际解析出的阶数（readback 探针，只读） |

## 关键参数详解

### `coefficients` 的格式

- 用逗号、空格或制表符分隔的一串浮点数，例如：`"0.25, 0.5, 0.25"`（3 阶平滑低通）。
- 解析在 `prepare` 时进行，上限 `FIR_MAX_TAPS = 1024`，超出部分截断。
- **空串或解析失败时退化为 1 阶直通**（系数 `[1.0]`），即原样输出——这是安全兜底，不是报错。
- `coeffs[0]` 作用于最新样本，`coeffs[taps-1]` 作用于最旧样本（标准卷积顺序）。
- 设计工具（MATLAB / scipy `firwin` / 仓库脚本 `scripts/pooliir2sos.py` 之类的外部流程）导出的系数数组直接粘贴成逗号分隔即可。

### `taps` 探针

`taps` 是 readback 探针（`update_policy: immediate`，只读）：prepare 后可通过 GET 读到实际生效的阶数，用于确认系数串被正确解析（比如解析失败时会读到 1）。

## 注意事项

- 修改 `coefficients` 需要重启（restart_required）：运行时不能换系数。延迟线内存在 prepare 分配、destroy 释放。
- 群延迟：对称 FIR（线性相位）的群延迟为 `(taps-1)/2` 个样本。组件本身声明 `latency_samples: 0`（实现未做延迟补偿/报告），与其它链路对齐时需自行考虑。
- 计算量为每样本每通道 `taps` 次乘加；高阶数（如 512+）在长块上成本可观，嵌入式部署请按算力预算选阶数。
- 各通道共享同一组系数、各自独立的延迟线（`delay` 缓冲为 `taps × channels`，交错布局）。

## 典型用法

```yaml
# 3 点滑动平均（最简低通）
- id: smooth
  component: orpheus.builtin.fir
  params: { coefficients: "0.25, 0.5, 0.25", channels: 2 }

# 65 阶线性相位低通（系数由外部设计工具生成）
- id: lp65
  component: orpheus.builtin.fir
  params:
    coefficients: "-0.0012, 0.0000, 0.0023, ..."
    channels: 2
```

## 实时安全

- `process` 无内存分配、无锁、无 IO，纯乘加 + 环形缓冲取模，支持就地处理（supports_inplace）。
- 所有堆内存（coeffs / delay / pos）都在 prepare 分配；reset 只清零延迟线和写指针。
