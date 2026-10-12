# indexed_tone - 索引式音调控制

通用 raw-filter tone 原语：把 0..255 索引映射到 boost/cut dB，使用逐样本指数 ramp 平滑增益，并执行：

$$
y=x+(G-1)f(x)
$$

`f(x)` 是一阶或二阶 raw DF2T section，boost/cut 可使用不同系数。支持 freeze、dB/s 或 ms ramp，以及跨 unity 的两阶段切换。

## 关键参数

- `gain_index/offset/scale`：$u=clamp((index-offset)/scale,-1,1)$。
- `max_boost_db/max_cut_db`：正负侧最大影响。
- `boost_coefs/cut_coefs`：`[b0,b1,b2,a1,a2]`。
- `filter_order`：1 或 2。
- `ramp_ms/ramp_db_per_s/freeze`：块边界控制，音频块内逐样本推进。

组件面向可复用的索引式 Bass/Treble/Mid，不绑定具体车型或模型命名。
