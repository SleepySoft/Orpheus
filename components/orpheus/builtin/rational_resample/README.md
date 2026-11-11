# orpheus.builtin.rational_resample — 有理倍采样率转换

## 接口与实际调用

`in` 是 `process: true` 的音频入口；每次接收完整 `input_frames` 帧，
执行 L/M 多相 FIR 后，算法显式交付 1 号 `out`。
没有独立读取 tick，也不等待水位、会合或调度器补调。
输入长度乘 `up` 必须能被 `down` 整除，输出帧数与采样率均为输入的 `up/down`。
默认 `416` 帧、`up=3/down=13`，每次输出 `96` 帧；
输入为48kHz时，输出为精确的 `144000/13 Hz`。

| 参数 | 默认值 | 范围 |
|---|---|---|
| `channels` | 2 | 1～64 |
| `up` | 3 | 1～512 |
| `down` | 13 | 1～512 |
| `taps` | 128 | 每相16～1024抽头 |
| `input_frames` | 416 | 1～65536 |

参数均须重启后生效。显式载荷不覆盖节点处理量子；
下游应声明匹配的输出载荷，不能把 Task 的处理帧数当成转换结果长度。
需改变输入批次时，使用独立读写的组块器，而不是截断样本数。

## 数学与时间

保留原 Blackman 窗 sinc 系数生成、逐相归一化、历史环与双精度累加顺序，
滤波历史跨调用保留。FIR 存在群延迟，不以移动时间戳掩盖。
`up == down` 时逐位复制，保留负零、NaN载荷及无穷，不推进滤波历史。

绝对输入帧位置须对齐约分后的 `down/gcd(up, down)`；
输出位置按先除后乘映射，避免可表示结果被中间乘法溢出误拒绝。
输入/输出末尾和映射溢出均明确报错，epoch及时间线标志继承实际输入。
`DISCONTINUITY` 标志不自动清空滤波历史；生命周期 `reset` 才清空历史与游标。

## 资源与错误

prepare 分配系数表与历史环，process 不分配资源。
非法参数不覆盖已有配置；分配失败由生成壳清理，可重新 prepare。
输入、输出不得相互重叠，也不得与 State、系数或历史存储重叠。
非法载荷、时间及别名在改变滤波状态前拒绝；
零处理量子不推进、不交付，下游失败不回滚已消费的滤波历史。

[component.yaml](component.yaml) 为接口事实源，使用独立脚本生成：

```powershell
python scripts\generate_component.py components\orpheus\builtin\rational_resample\component.yaml
```

原生测试保留频响、阻带、DC、reset与分块一致性基线；
图级测试用独立直接卷积预期检查动态/生成代码的 global/Task 四种入口，
覆盖降采样、插值、64通道及同实例一拍多次真实交付。
