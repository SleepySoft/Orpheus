# 占位模块

用同一个预编译组件搭建尚未实现的音频骨架。人工拖入即可使用，不需要为每个模型节点编写空 C 实现。

**它不是算法实现。** 画布显示“待蒸馏”，编译 plan 有 warnings，prepare 输出占位日志；透传/零值及其下游结果不能作为模型等价证据。既有 `package_type: placeholder` 研究组件仍不可编译。

## 配置输入输出

参数面板的“占位接口”编辑器可增加/删除输入输出、修改名称和各自通道数；输入/输出可只有一侧，每方向最多 32 个端口，通道数 1–64，端口名称在整个模块中唯一。

接口在 `params.interface` 中保存为 JSON 字符串，例如：

```json
[
  {"id":"mic","direction":"input","channels":2},
  {"id":"processed","direction":"output","channels":2},
  {"id":"meter","direction":"output","channels":1}
]
```

目前只支持同 Task 的 f32 音频，采样率与块长沿用框架推导。不宣称支持任意数据类型、事件、控制事务或变速；这类需求先保留严格研究占位并记录能力缺口。

| 参数 | 默认 | 意义 |
|---|---|---|
| interface | 一个双通道输入和输出 | 按数组顺序分别确定输入、输出槽位 |
| mode | passthrough | 路由透传；silence 为所有输出置零 |
| routes | `"0"` | 按输出顺序填输入序号，0 起算，-1 置零 |

上面接口配 `routes: "0,-1"`，则 mic 透传至 processed，meter 置零。透传两端通道数必须相等，路由数量必须等于输出数量；不自动混音、缩放或补通道。被路由的输入断线会返回错误，不静默输出零。

只有输入的 sink 用空 routes；只有输出的 source 可用 silence 或全 -1 路由。占位模块自身不是时钟源。修改接口/行为后需重新编译或启动，不作实时参数更新。

## TODO 块与查找

选中节点，在参数面板填写 TODO：任务、源码索引、源 hash、意图、下一步、验收条件及阻塞。可有多项，状态为 pending/in_progress/blocked/done。蒸馏任务不要只留一句“待实现”，必须能让下一轮直接恢复。

TODO 写在 `graph.nodes[].todos`，不是参数或仅在节点笔记中；改换成真正组件时保留节点 ID 和 todos，验证后标 done：

```yaml
todos:
  - id: filter
    title: 抽取滤波算法并验证
    status: pending
    source_ref: "analysis/index.yaml#front；src/model.c:FrontProcess"
    source_hash: "源版本或 SHA-256"
    intent: 双通道独立状态滤波
    next_step: 对照 fir 的系数方向及跨块状态
    acceptance: 非零、多块、reset golden；动态/生成逐字节一致
    blocker: ""
```

查找全部开放项：

```powershell
python -m orpheus_core.cli todos workspace\my_model --json
python -m orpheus_core.cli todos workspace\my_model --all --json
python -m orpheus_core.cli todos workspace\my_model --check
```

也可使用左侧“TODO”面板（包含未保存编辑）或 `GET /api/projects/{name}/todos?include_done=true`（已保存工程）。

清单覆盖主图与所有加载的子图定义，包括未实例化定义；共享定义只列一次，不按展开实例重复。严格占位 blockers 同样可见。即使删除所有 TODO 或都标 done，只要节点仍是占位模块，清单仍有自动未实现项。`--check` 存在开放项返回 1。

清单不扫描任意源文件里的 TODO 注释；需要执行的外部文本待办应归并到结构化 TODO。独立节点笔记用于补充背景，不是另一个任务权威。
