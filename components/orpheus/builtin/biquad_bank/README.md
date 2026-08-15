# orpheus.builtin.biquad_bank — 双二阶滤波器组

## 功能

两段 peaking（峰值 EQ）biquad 的串联滤波器组：`x → 第 1 段 → 第 2 段 → out`。两段参数独立（fc/q/gain_db），合成一个 4 阶 EQ。

它是 Orpheus **v2 聚合注册的试点组件**：内部物理内嵌 2 个 `BiquadState` 子块（通过 manifest `deps: orpheus.builtin.biquad` 复用其头文件），由父组件的 `register_slots` 代理注册子块的参数字段（`fc0`、`q0` 等层级键），并把每段的 5 个系数（b0, b1, b2, a1, a2，内存中连续）暴露为 **BULK 槽**（`bq0.coefs` / `bq1.coefs`），支持 Runtime 在运行中直写系数。

## 端口

| id | 方向 | 类型 | 说明 |
|---|---|---|---|
| in | input | audio | 输入音频，`channels` 通道 |
| out | output | audio | 两段串联滤波后的音频，通道数与输入相同 |

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `fc0` | float | 1000.0 Hz | 第 1 段中心频率，20~20000 Hz |
| `q0` | float | 1.0 | 第 1 段 Q，0.1~10.0 |
| `gain_db0` | float | 0.0 dB | 第 1 段增益，-24~+24 dB |
| `fc1` | float | 3000.0 Hz | 第 2 段中心频率，20~20000 Hz |
| `q1` | float | 1.0 | 第 2 段 Q，0.1~10.0 |
| `gain_db1` | float | 0.0 dB | 第 2 段增益，-24~+24 dB |
| `channels` | int | 2 | 通道数，1~32；改变后需重新编译（affects_signature） |
| `form` | string | df2t | 滤波结构（两段共用）：`df2t`=DF-II 转置（滚动，推荐）；`df1`=传统直接 I 型。两者传递函数相同 |

所有普通参数均为 `restart_required`：它们在 `prepare` 时按 RBJ peaking 公式（与 `biquad` 组件同式）折算成系数。**运行中改 EQ 不走这些参数，而走下面的 BULK 系数槽。**

## BULK 系数槽（运行时直写）

| 槽 | 类型 | 数量 | 说明 |
|---|---|---|---|
| `bq0.coefs` | float | 5 | 第 1 段系数 `[b0, b1, b2, a1, a2]`，double_bank |
| `bq1.coefs` | float | 5 | 第 2 段系数 `[b0, b1, b2, a1, a2]`，double_bank |

### BULK + 双 bank 的意义

这是本组件的核心机制，回答"如何在不重启的情况下换一整套滤波系数，又不产生爆音"：

1. **BULK 直写**：外部（UI / 上位机 / 自适应算法）自己算好 5 个系数，通过 `Runtime::write_bulk` 一次性写入，绕过"参数→prepare→重算"的链路。系数语义完全交给写入方（可以写入任何二阶节系数，不限于 peaking）。
2. **双 bank（影子 + 块边界提交）**：槽带 `ORPHEUS_SLOT_DOUBLE_BUFFERED` 标志。写入落在**影子区**，正在运行的音频块仍读旧的 active 系数；直到当前块处理完的块边界，Runtime 才一次性提交（影子→active）。这样 5 个系数是**原子生效**的——绝不会出现"b0 是新的、a1 还是旧的"这种中间态，避免瞬态爆音/发散。
3. **工程可关**：双 bank 行为受工程级开关（auto/on/off）控制。off 时 `write_bulk` 直写 active 即时生效、零额外内存，适合存储紧张的嵌入式部署。

## 注意事项

- 两段固定为 peaking 形态（prepare 内的默认系数计算），但 BULK 直写可以覆盖为任意二阶系数。
- 直写系数时注意：差分方程中的递推项是 `-a1·y₁ - a2·y₂`（v1.1.0 起与 biquad 组件同为标准 DF-I/DF-II 转置结构，可选 `form`），写入的 a1/a2 符号应与 RBJ 归一化约定一致（分母 `1 + a1·z⁻¹ + a2·z⁻²`）。
- 换系数不会清零滤波器历史（z1/z2），若新旧系数差异很大，提交瞬间可能有轻微过渡痕迹；通常块边界提交已足够平滑。
- 越界写入会被 Runtime 拒绝（槽声明了 `count: 5`）。
- 段数固定为 2（`BIQUAD_BANK_STAGES = 2`），是聚合注册的试点而非通用 N 段实现；更多段数请用多个 biquad/biquad_bank 串联。

## 典型用法

```yaml
- id: eq2
  component: orpheus.builtin.biquad_bank
  params:
    fc0: 200      # 低频隆隆声压一点
    q0: 1.0
    gain_db0: -3.0
    fc1: 4000     # 临场感抬一点
    q1: 1.2
    gain_db1: 2.5
    channels: 2
```

运行时调音：UI/宿主向 `eq2` 的 `bq0.coefs` 写入新系数数组 → 影子区 → 下一个块边界原子生效，无重启、无爆音。

## 实时安全

- `process` 无内存分配、无锁、无 IO；系数读取与影子提交由 Runtime 在块边界完成，实时线程内只做纯乘加。
- 双 bank 影子内存在 prepare 阶段统一分配，不在实时路径。
