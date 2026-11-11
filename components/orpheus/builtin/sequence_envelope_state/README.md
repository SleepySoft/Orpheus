# 序列锁存增益状态

`targets` 是唯一驱动入口，其余输入为本次调用读取的控制数据：
`sequence` 为i32位型，按uint32序列比较；`coefficient` 仅首次锁存；
`feedback` 为已绑定的1帧 `timing: latest` 数组。
每个数组含 `channels` 个元素（1～128，默认20）。

- 首次调用从targets初始化当前值和已接受目标，并锁存系数。
- 奇序列输出freeze=1，不接受新目标；重复序列也不更新目标。
- 变化的偶序列接受新目标，并将当前值逐通道抬高到silent下界。
  silent默认为5.0118723e-7，必须有限且非负。
- 序列位型完整比较，允许重复、回退和uint32回绕，不推断丢帧或顺序。

算法先写完全部输出，再按 **accepted(2) → alpha(3) → freeze(4) → start(1)**
实际交付。编号只对应引脚，不表示调用优先级；
连接到非process输入只提供数据，连接到process输入才驱动下游。
同一输出的多个消费者按工程保存顺序同步完成各自子树。

全部出口子调用成功返回后，重新检查feedback缓存并提交其最新值为下一次start。
这不是块末控制链路，不增加一块延迟，也不等待会合或去重。
出口失败会停止后续出口，不提交feedback；已经完成的初始化、序列接受及子调用不回滚。
返回时feedback变为无效形状也明确失败，禁止用入口时的旧值冒充新反馈。

零处理量子不改变状态、不交付；reset清除状态，保留参数配置。
出口可悬空但状态仍推进。输入/输出、输出之间及与State的存储重叠明确拒绝；
process不分配资源。非有限目标和系数不做隐式裁剪，首次锁存保留原始位型。

接口事实源为 [component.yaml](component.yaml)，手写算法位于
[user/algorithm.c](user/algorithm.c)，独立生成命令：

```powershell
python scripts\generate_component.py components\orpheus\builtin\sequence_envelope_state\component.yaml
```

原生测试检查真实出口顺序、先发布全部数据、反馈返回后提交、失败不回滚、
零量子、reset、别名、非法参数和悬空出口。
图级测试在1/20/128通道下验证奇偶/重复/回绕序列、系数锁存与一拍多次调用，
使用独立状态机预期覆盖动态/生成代码的global/Task四种入口。
