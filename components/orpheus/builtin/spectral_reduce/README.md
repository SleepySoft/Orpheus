# orpheus.builtin.spectral_reduce — 谱减聚合

## 功能

STFT 分析链的收尾组件：把"每块 `num_frames` 帧 × 每帧 `fft_size` 个槽位"的展平频谱流，按通道聚合成**单一标量**（sum / mean / min / max），再把该标量**广播填满整个输出块**。

```
输入布局（每通道）：[ 帧0: bin0..binN | 帧1: bin0..binN | ... ]  每帧步长 fft_size
聚合范围：num_frames 帧 × 前 bin_count 个 bin
输出：每通道一个标量，重复写入所有输出样本
```

典型角色：在 `circular_buffer → window → rfft(power) → spectral_reduce` 链路末端，把每个块的频谱压成一个特征值（如平均功率、最小功率），供下游阈值判断/噪声估计使用——RNC TID5/TID6 即用它做谱减法的谱统计（TID5 用 `min` 估噪声底，TID6 用 `mean` 算平均功率）。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | 展平频谱流，`channels` 通道 |
| out | output | audio | 每通道标量广播填满的输出块，通道数与输入相同 |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `channels` | int | 2 | 通道数，1~64；改变后需重新编译（affects_signature） |
| `fft_size` | int | 128 | FFT 点数（帧步长），2~4096；restart_required，affects_signature |
| `num_frames` | int | 12 | 每块帧数，1~256；restart_required，affects_signature |
| `bin_count` | int | 0 | 参与运算的 bin 数，0~4096；**0 = 自动取 `fft_size/2 + 1`**（实数 FFT 的有效 bin 数）；restart_required |
| `operation` | string | "mean" | `sum` / `mean` / `min` / `max`；restart_required |

## 关键参数详解

### `fft_size` / `num_frames`：必须与上游配套

组件不解析频谱格式，只按固定步长索引：`in[(f·fft_size + k)·ch + c]`。`fft_size` 必须等于上游 `rfft` 的 `fft_size`（rfft 每帧输出占 fft_size 个槽位），`num_frames` 必须等于上游 `circular_buffer` 的 `num_frames`。三者任一不匹配，聚合的就是错位的数据。

### `bin_count`：为什么默认是 fft_size/2+1

实数 FFT 的频谱关于奈奎斯特频率对称，有效信息只有前 `fft_size/2 + 1` 个 bin（含直流与奈奎斯特点）。`bin_count = 0` 时自动取该值；也可以设小只看低频段（如前 10 个 bin 做低频能量检测）。

### `operation` 的语义选择

| 值 | 含义 | 典型用途 |
|---|---|---|
| `sum` | 所有帧 × bin 求和 | 总能量 |
| `mean` | 平均值（sum ÷ 样本数） | 平均功率，与块长无关的归一化度量 |
| `min` | 最小值 | 噪声底估计（谱减法常用"最小值跟踪"思想） |
| `max` | 最大值 | 峰值/最强谱线检测 |

注意聚合是**跨帧 × 跨 bin 一起**做的：结果反映整个块的统计，而不是逐帧或逐 bin。

## 注意事项

- 输出不是"每帧一个值"，而是**每通道一个值、广播填满整个输出块**（输出块长 = 输入块长）。下游取任意一个样本即得聚合结果，或配合探针/阈值组件使用。
- 输入索引越界保护：超出实际块长的 `(帧, bin)` 会被跳过（不计入 mean 的样本数），但正常配套使用不会触发。
- 本组件对数值本身无语义假设：输入是 halfcomplex、幅度还是功率谱由上游 `rfft` 的 `output_mode` 决定，通常应选 `power` 或 `magnitude`（halfcomplex 的交错实虚部做 sum/mean 没有物理意义）。
- 全部参数 restart_required；不支持运行时切换 operation。

## 典型用法

```yaml
# TID5：噪声底估计（最小功率）
- id: sr
  component: orpheus.builtin.spectral_reduce
  params:
    channels: 12
    fft_size: 128        # 与 rfft 一致
    num_frames: 24       # 与 circular_buffer 一致
    bin_count: 65        # = 128/2 + 1
    operation: min

# TID6：平均功率
- id: sr
  component: orpheus.builtin.spectral_reduce
  params: { channels: 4, fft_size: 256, num_frames: 48, bin_count: 129, operation: mean }
```

（完整链路见 `examples/symphony_asm_ehc_rnc.yaml` 的 tid5/tid6 任务。）

## 实时安全

- `process` 无内存分配、无锁、无 IO，纯遍历累加；状态无量（仅缓存参数）。
- 不支持就地处理（supports_inplace=false）。
