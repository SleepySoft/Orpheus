# B.MIX_DECI ORGANIZE / ROUTE 蒸馏记录

> 状态：`B.MIX_DECI.ORGANIZE` 与 `B.MIX_DECI.ROUTE` 已验证并替换 placeholder；父级仍因 ALIGN/RAMP 未实现而保持 pending。

## 源码边界修正

权威源：`Model_1_2_ert_shrlib_rtw/Model_1_2.c`，索引 SHA-256 已校验。

原索引把 ORGANIZE 简化为 21→21，把 RAMP/ROUTE 简化为单一 52ch 边界。生成代码实际边界是：

```text
ALIGN 21ch
  -> ORGANIZE
       ├─ CS 17ch
       └─ shared LR 21ch
  -> RAMP
       ├─ Center 2ch
       ├─ Left 25ch
       └─ Right 25ch
  -> ROUTE
       ├─ Peripheral 22ch
       ├─ Headrest 10ch
       └─ Overhead 20ch
```

该修正已写回 Algorithm IR 和父 composite 标准音频边。

## ORGANIZE

输入的一次序为：

```text
Lbass,Rbass,Lo,Ro,Ls,Rs,Limp,Rimp,Ltail,Rtail,
Lftail,Rftail,Lrtail,Rrtail,Csi,Lsi,Rsi,Ltfi,Rtfi,Ltbi,Rtbi
```

两个 0-based gather：

```text
CS17 = [0,2,4,6,8,15,17,18,1,3,5,7,9,16,19,20,14]
LR21 = [0,2,4,6,8,10,12,15,17,18,1,3,5,7,9,11,13,16,19,20,14]
```

实现为 21ch 直通入口扇出到两个通用 `channel_router`。CS 排除输入 10..13 的四路 wiggle tails；LR 保留。

## ROUTE

RAMP 原生输出布局：Center2、Left25、Right25。ROUTE 的精确分发：

- Peripheral22：Center `[0..1]` + Left `[0..9]` + Right `[0..9]`
- Overhead20：Left `[10..19]` + Right `[10..19]`
- Headrest10：Left/Right `[20..24]` 逐对交错

实现只使用通用组件：

```text
Center2 -> deinterleave ─┐
Left25   -> deinterleave ├-> interleave22 / interleave20 / interleave10
Right25  -> deinterleave ┘
```

共 52 条单声道标准音频连接；每个输入恰好使用一次，无重复、无遗漏。

## 验证

`test_b_mix_deci_organize_and_route_match_source_permutations` 验证：

- ORGANIZE 两个索引表逐项一致；
- RAMP 接口为 CS17/LR21 → Center2/Left25/Right25；
- ROUTE 52 个 one-hot 输入全部落到预期输出位置；
- 52 个源 endpoint 唯一，三个输出集合互斥并覆盖全集。

`deinterleave/interleave` 的可变引脚展开和端到端交换已有 `test_variable_ports.py` 覆盖。

## 六门禁

| 模块 | boundary | schedule | algorithm | state | parameters | golden |
|---|---|---|---|---|---|---|
| ORGANIZE | verified | verified | verified | verified（无状态） | verified（源码常量） | verified（21 路 one-hot） |
| ROUTE | verified | verified | verified | verified（无状态） | verified（源码常量） | verified（52 路 one-hot） |

`B.MIX_DECI.ALIGN` 与 `B.MIX_DECI.RAMP` 不因本记录升级完成度。
