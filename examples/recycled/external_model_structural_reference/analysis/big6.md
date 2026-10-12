# A.BIG6 初始分析记录

状态：边界、第一轮 process/state 和默认参数已提取，golden 尚未完成，禁止并入完整系统图。

本轮证据固定为：

- `Model_1_1.c` SHA-256 `266a97f1f43276b7f93d94a2cb35981b6089a4c4905faf7814a9342e532e0889`
- `Model_1_1_PreAmp_p0_b0_TOP.c` SHA-256 `f7fe36b16a469f3a333fd8bee974853e19e1b959ce734bed325d9206765c3390`

## 用途与所有权

Big6 在该模型中不是一个连续的 2 路效果器：

- `A.BIG6_VOLUME`、`A.BIG6_TONE`、`A.BIG6_BALANCE` 属于 Runtime A 的 PreAmp 主节目链，处理 InputMixer 产生的 10 路内部声场。
- Fade 属于 `A.FADE_SUM`，位于全速率与降速空间结果合并之后，处理 22 路扬声器信号。
- Bass/Mid/Treble 的用途是修改节目音色；Balance 调整左右声像组；Fade 调整前后声场。它们的目标通道集合不同，不能为了 UI 上像“六个旋钮”而强行放进同一个串行 2 路组件。

## 已确认链路

```text
12 路节目
  -> 输入布局转换（10 路内部声场）
  -> Volume（10 路平滑增益）
       |-> 32 路主 PostProcess Buffer（只使用前六路映射）
       `-> Bass -> Treble -> Mid（10 路音调）
           -> Balance（10 路、四个 ramper 状态）
           -> 10 路自适应响度/跨 Runtime 抽头

22 路全速率结果 + 22 路降速空间结果
  -> 求和 -> 扬声器延迟 -> 路由
  -> Fade（22 路前后组） -> Mute -> 反馈/输出
```

这解释了为什么之前把 Bass/Mid/Treble/Balance/Volume 放在一条 2 路支路并接到 sink 是错误的：它把真实 10 路控制降成了演示级立体声效果，也遗漏了 Volume 后的主输出分支。Tone/Balance 并非在当前帧直接串入主 PostProcess，而是通过自适应响度、Runtime B 和 22 路反馈闭环影响系统；详见 `preamp-routing.md`。

## 第一轮 process/state 结论

主链调用锚点位于 `Model_1_1.c:8706..8853`，顺序不是 UI 常见排列，而是：

```text
Volume -> Bass -> Treble -> Mid -> Balance
```

### Volume

- `Model_1_1.c:8252` 的 process 对 32 个样本逐样本执行 `gain *= rampCoeff`，再乘到映射给该 ramper 的通道。
- 状态至少包含 `currentGain`、`targetGain`、`rampCoeff`、`frameCount` 和 `Freeze`。
- `frameCount == 0` 时直接吸附到 `targetGain`；freeze 时保持当前增益，不推进帧计数。
- 输出还包含逐通道 `gainOut`，后续电平检测会读取它，因此实现不能只处理音频而丢掉当前增益状态。
- 当前生成实例是一个 ramper 覆盖 10 路音频；编码为 `[10,1,2,3,4,5,6,7,8,9,10]`，仍需加入 golden。

### Bass / Treble / Mid

- 三段调用顺序由 `Model_1_1.c:8710..8726` 直接确定。
- Bass 和 Treble 分别读取 3 项 boost/cut 系数；Mid 读取 5 项 boost/cut 系数，不能先验假设三者都是同一种一阶 shelf。
- 控制索引先按 `(index - offset) / scale` 归一化到 `[-1, 1]`，再结合最大 boost/cut 和各自 ramper 参数更新滤波器状态。
- TOP 字段中的 boost/cut 系数、最大 dB、offset、scale 和 ramp time 已落盘到 `../evidence/big6-defaults.json`；滤波器状态递推仍待下一轮提取。

### Balance

- `Model_1_1.c:8727..8853` 对 10 路音频使用 4 个独立 ramper，并由 `BalanceChanRampMap[10]` 决定通道归属。
- 控制索引通过 30 点 `BalanceTableIdx/BalanceTableDb` 在 dB 域插值出衰减侧增益。
- `BalanceBoostDisable == 0` 时，另一侧按 $\sqrt{2-cut^2}$ 补偿能量；并非通用 `balance` 的固定线性淡化。
- Center 和 Mono 目标取左右增益平均值；极端位置可把衰减侧、Center，乃至 Bass 组静音。
- 每个 ramper独立维护 current/target/ramp/frameCount，支持 freeze、毫秒时间和 dB/s 速率两种更新入口。

因此现有 `orpheus.builtin.balance` 只能作为交互原型，不能作为该模块的数值实现。较可能的实现是复用 `gain_ramper` 数据面并新增索引表/四组目标计算子模块，或扩展 `balance` 以显式支持四组映射与能量补偿。

## 复用候选与未决问题

| 索引模块 | 可复用组件 | 使用前必须验证 |
|---|---|---|
| `A.BIG6_VOLUME` | `gain_ramper` | 控制值是 dB 还是线性增益、斜坡律、10 路 ramper 映射、current gain 读回 |
| `A.BIG6_TONE` | `bass` / `midrange` / `treble` | 原滤波器公式、频点/Q、boost/cut 参数映射、每通道系数是否相同 |
| `A.BIG6_BALANCE` | `balance` 或 `gain_ramper` 组合 | 四个 ramper 分别控制哪些通道、中心/低频/顶置通道如何处理 |
| `A.FADE_SUM` | `fade` / `sleeping_beauty` / `gain_ramper` | 前后通道分组、低频保留规则、2 元素 FadeGains 的定义和跨 Runtime 用途 |

## 下一轮证据提取

1. 继续还原三段 tone 的差分方程、状态布局和 boost/cut 插值更新。
2. 生成非零 Volume/Tone/Balance/Fade 输入向量，获取参考输出与 current gain 状态。
3. 分别验证静态响应、跨块斜坡、freeze 和 reset，再决定复用通用组件还是实现模型专用子模块。
