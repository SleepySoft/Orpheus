# A.INPUT_SELECT 蒸馏记录

> 状态：已验证并替换 placeholder
>
> 实现：通用 `gain` + 四个 `input_select`

## 算法边界

```text
30ch input
  -> shared route30 (TOP base + RTC override)
       ├-> entertainment: channels 1..12
       ├-> announcement: channels 13..29
       └-> microphone: channel 30
```

顺序必须是先对共同 30ch 总线应用 route，再做三个 selector；不能拆成三个互不共享路由状态的选择器。

## 源码证据

- `Model_1_1.c`：逐项使用 RTC 非零值覆盖 TOP `routerOutMap`；路由值无效时整路置零；三类输出随后从共享结果选择。
- `Model_1_1_InputSelect_p0_b0_TOP.c`：
  - base route = 1..30
  - entertainment = 1..12
  - announcement = 13..29
  - microphone = 30
- `evidence/input-routing-defaults.json` 固定全部值与 SHA-256。

## 通用能力

`orpheus.builtin.input_select` 新增 `override_select`：

- 0：沿用同位置基础 `select`
- 1..channels_in：使用 1-based 覆盖值
- 负数或越界：静音
- `block_boundary` 更新，通过 `set_parameter` 重建有效 map

该语义可复用于任何“静态基础路由 + 稀疏运行期覆盖”场景。

## Composite

- `ingress`: 30ch 0dB gain，用于合法扇出
- `route`: 30→30 input_select，base=1..30，override 默认全 0
- `entertainment`: 30→12，select=1..12
- `announcement`: 30→17，select=13..29
- `microphone`: 30→1，select=30
- 公开参数 `router_override` 映射到 `route:override_select`

## 验证

- Algorithm IR 测试检查共享 route 必须位于三个 selector 之前；
- `test_override_zero_keeps_base_and_invalid_mutes` 直接调用组件 ABI：
  - 初始 1,2,3,4 直通；
  - override `0,4,99,-1` 得到 `1,4,0,0`；
- 项目 flatten 后只包含通用组件。

## 六门禁

| 门禁 | 状态 |
|---|---|
| boundary | verified |
| schedule | verified（A0） |
| algorithm | verified |
| state | verified（共享 base/effective map） |
| parameters | verified |
| golden | verified（one-hot + runtime override） |
