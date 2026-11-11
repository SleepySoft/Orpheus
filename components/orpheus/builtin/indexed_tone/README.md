# indexed_tone - 索引控制均衡

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

## 接口与状态

`component.yaml` 的 `interfaces` 是唯一声明源，独立脚本生成 `generated/`，
手写实现位于 `user/algorithm.c`。`in` 是唯一驱动入口，每次真实到达独立处理，
由算法交付 1 号 `out`，不按 Task 去重；下游失败不回滚已更新的滤波/包络历史。

- `execution_mode=0`：按索引请求计算目标；跨 unity 时先到达 unity 邻域，
  再配置最终目标。系数支路逐样本选择，块结束再吸附目标和切换阶段。
  reset 清滤波历史、取消剩余斜坡，但保留当前增益和目标。
- `execution_mode=1`：`phase` 与两组 `ramp*_current_gain/target_gain/coefficient/remaining_blocks`
  是真实可读写状态。阶段 0 计数为零且阶段 1 仍为正时自动切换；
  否则零计数吸附目标，非零计数递减并逐样本递乘，`INT32_MIN` 饱和。
  支路按本次块起始增益选择，不在块内切换；一阶反馈符号和二阶运算顺序
  与索引模式不同，不能合并实现。reset 只清滤波历史，保留显式阶段与包络。
- 两种模式的 `freeze` 都只冻结包络，不停止滤波历史推进。
  零帧仍交付事件：显式模式不推进，索引模式保留块末计数递减/阶段切换，
  不统一增加早退。显式模式拒绝运行期索引和斜坡时间/速率请求。

两个五项 f32 系数资源由声明绑定私有存储，支持双 Bank 热更新；
字符串只是初始化配置，不重复注册资源。
两种模式现在都要求完整的五项有限数值 CSV，拒绝垃圾、缺项与多项；
不再保留原索引模式的部分解析/静默补值。
精确原地合法，部分重叠、与状态重叠和不足载荷明确拒绝；
分配声明仍保守使用 `supports_inplace: false`。
