# orpheus.builtin.exponential_gain — 指数增益斜坡

## 数值与控制语义

所有通道共享一条逐样本指数包络。调用开始以 `max(current_gain,min_gain)` 为起点；
目标差超过 `snap_threshold` 且 `remaining_blocks>0` 时进入斜坡，每次非空调用只减一次
`remaining_blocks`。首次斜坡或目标变化时加载 `ramp_coefficient*起点`，
随后每个样本乘同一乘数；同目标重请求保留滤波状态，不重新起坡。
没有剩余块或已吸附时整块使用 `target_gain`，不自动把毫秒换成请求。

运行期 float 参数：`target_gain`、`ramp_coefficient`、`remaining_blocks`；
三者保持 `bindable/readback/persistent`，`remaining_blocks` 同时是控制源。
剩余块数必须是 `[0,16777216]` 内的整数。`current_gain` 是只读 float 探针和控制源，
槽地址与 getter 读取同一状态。`channels`、`initial_gain`、`min_gain`、
`snap_threshold` 是重启参数；最小增益必须为正，其余增益及阈值必须非负。

prepare 用 `initial_gain` 初始化当前增益；reset 恢复初始增益、清空剩余块数和
私有延迟目标/滤波状态，保留目标和乘数。零帧调用只交付，不推进包络。
未连接输入按零处理；未连接输出仍推进状态并交付空 buffer，不抑制调用。

## 配置驱动接口（ABI v7）

`component.yaml` 的 `interfaces` 是端口、参数与探针的唯一事实源。修改后运行：

```powershell
python scripts\generate_component.py components\orpheus\builtin\exponential_gain\component.yaml
```

`generated/` 仅由脚本生成元数据与 ABI 壳，`user/algorithm.c/.h` 保留手写指数核与
私有滤波状态。`in` 是唯一驱动输入，稳定出口编号为 `1`；算法在原计算就绪点显式
`emit_out`，通过 delivery 交付，不等待会合、不去重。无旧 descriptor 或 next/branch 包装。
实例状态由宿主按生成 descriptor 的大小与对齐提供，process 不分配内存。

参数按声明严格校验；越界、非有限值或错误类型返回 `ORPHEUS_ERR_INVALID_ARG`，
不再隐式 clamp/类型转换；额外的非负、整数块数约束由手写生命周期钩子验证。

## 验证

`common_components_delivery_smoke` 检查独立数值、参数更新、probe 槽及同步交付；
`exponential_gain_smoke` 覆盖 64 通道、原地计算、重入、吸附和可选引脚；
`orpheus_core/tests/test_exponential_gain.py` 检查动态/生成图、全局/Task 四入口逐位一致性。
