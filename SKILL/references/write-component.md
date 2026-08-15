# 编写 Orpheus 组件

## 目录

- 检查清单（照做即可）
- component.yaml 字段详解
- C 实现模式（含要点）
- 可变引脚组件
- UI 控件元数据
- 验证步骤

## 检查清单

1. 复制最相近的现有组件目录（`components/orpheus/builtin/<name>/`，4 个文件：component.yaml、src/x.c、include/x.h、CMakeLists.txt）
2. 改 id（`orpheus.builtin.<name>`，小写+下划线）、name（中文）、category
3. 声明 ports/parameters/memory/execution
4. 实现 C 函数表（见下，重点：入口宏、prepare 读参数、process 判空）
5. CMakeLists 改 COMPONENT_NAME 为 `orpheus_builtin_<name>`
6. `python -m orpheus_core.cli build`（顶层 CMake GLOB 自动发现新目录；build 会重新 configure）
7. 写一个示例工程跑通（见「验证步骤」）

## component.yaml 字段详解

```yaml
id: orpheus.builtin.example      # 必须匹配 ^[a-z0-9_]+(\.[a-z0-9_]+)+$
name: 示例组件                    # UI 中文显示名（必填）
category: 基础/滤波               # 多级路径，'/' 分层：顶层=基础(常用/教学)|音效|高级(公司项目)|平台；二级自由命名，如 基础/信号源 高级/频域
order: 1                         # 可选：叶内排序权重（小的在前，缺省按名字排）
description: 一句话说明
version: 1.0.0
abi_version: 1
package_type: source             # source | binary | composite
sources: [src/example.c]
headers: [include/orpheus_example.h]
ports:
  - id: in
    direction: input             # input | output
    type: audio                  # audio | control | bulk | debug
    sample_format: f32
    channels: param:channels     # 整数，或表达式 param:<参数id> / task:sample_rate / task:block_size / in:block_size
    count: param:channels        # 可选：可变引脚，展开为 in0..inN-1
parameters:
  - id: gain_db
    name: 增益
    type: float                  # float | int | bool | string
    default: 0.0
    range: [-96.0, 24.0]
    unit: dB
    update_policy: smoothed      # immediate|block_boundary|smoothed|transactional|restart_required
    affects_signature: false     # true=改变需重新编译（UI 通用参数区置顶；实时会话不推送）
    readback: false              # true=运行后可回读（探针值）
    widget: slider               # 可选 UI 控件：number|text|slider|select|checkbox|file
    options: [{value: a, label: 甲}]   # select 静态选项
    options_source: devices      # 动态选项（目前仅 devices）
    readonly: false
memory: { state_size: 0, scratch_size: 0, alignment: 8 }
execution:
  sample_rate_independent: true
  latency_samples: 0
  supports_inplace: true
  realtime_safe: true            # process 满足实时禁令才允许 true
```

## C 实现模式

照抄 `gain/src/gain.c` 的结构。必须正确的四点：

```c
/* 1. 入口：必须宏包裹（静态链接不冲突，动态加载用默认名） */
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &iface; }

/* 2. prepare 必须读初始参数（只读 config->channels 不够） */
static int prepare(void* state, const OrpheusConfig* config) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (!config->param_ids[i]) continue;
        if (strcmp(config->param_ids[i], "gain_db") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_FLOAT) { /* ... */ }
    }
    /* Runtime 传参规则：channels/sample_rate→INT；纯数字→FLOAT；其余→STRING */
}

/* 3. process 判空 + 实时禁令（无 malloc/锁/IO/printf） */
static int process(void* state, const OrpheusProcessContext* ctx) {
    if (ctx->input_count < 1 || !ctx->inputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    /* ... */
}

/* 4. 参数更新：update_policy 非 restart_required 的参数应在 set_parameter 里生效
      （实时运行中 UI 改参数会推送 SET 命令） */
```

## 可变引脚组件（参考 interleave/deinterleave）

- manifest：端口加 `count: param:channels`，引脚展开为 `<id>0..<id>N-1`
- C 侧：描述符里该端口声明一次（is_variable=true, channels_param="channels"）；process 遍历 `ctx->outputs[i]` / `ctx->inputs[i]`，数量取 prepare 存的 channels，**逐槽判空**（未连接引脚为 NULL）
- 编译器和 UI 会自动展开/刷新引脚，无需额外工作

## UI 控件元数据

- 参数控件默认按 type 推断（float/int→number，string→text，bool→checkbox）；用 `widget` 显式指定
- 文件参数用 `widget: file`（UI 提供工程内浏览/上传）
- 枚举参数用 `widget: select` + `options`（选项值必须与 C 代码接受的字符串一致）
- 节点本体扩展（如电平条）：在 `ui/src/nodeWidgets.js` 按组件 id 注册；回读值经 `data.probe` 传入

## 验证步骤

```powershell
python -m orpheus_core.cli build          # 编译出新 DLL
python -m orpheus_core.cli scan           # 确认被发现
# 写一个 examples/ 级别的小工程或用 UI 拖出来 → 编译 → 运行，确认行为正确
python -m pytest orpheus_core/tests/      # 回归
```

有探针参数（readback）时：离线运行后应出现在 run 响应 `probes` 和 UI 节点上。
