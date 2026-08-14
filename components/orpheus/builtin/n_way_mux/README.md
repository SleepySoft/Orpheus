# N路选择（n_way_mux）

从 N 路输入中动态选择一路输出，便于原声/处理后对比切换。

可变引脚 `in0..inN-1` 由 `inputs` 参数决定；只有被连接的输入会被选中，未连接的输入被跳过。

## 参数

| 参数 | 含义 | 默认 | 范围 |
|---|---|---|---|
| inputs | 输入路数（重启生效） | 2 | 1 .. 32 |
| channels | 通道数 | 2 | 1 .. 32 |
| select | 选择输入索引（小数=&gt;相邻输入间交叉淡化） | 0.0 | 0 .. N-1 |
| ramp_ms | 切换交叉淡化时间 | 20 ms | 0 .. 1000 |

## 使用场景

- A/B 对比：原声 + 处理后同时接入，`select` 切换直接多比对。
- 交叉淡化避免切换咔嘐声：`select` 为小数时在相邻两路间平滑过渡。
- 实时调参时 `ramp_ms`控制过渡运动速度。

## 示例

```yaml
  - id: mux
    component: orpheus.builtin.n_way_mux
    params: { inputs: 2, channels: 2, select: 0.0, ramp_ms: 20.0 }
  # ?? raw_out -> mux:in0, processed_out -> mux:in1, mux:out -> output
```
