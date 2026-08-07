# 工程与文档格式参考

## 目录

- project.yaml 格式
- 子组件（subcomponents）
- workspace 布局
- 路径规则

## project.yaml 格式

```yaml
version: "0.1.0"
metadata: { name: 示例, description: ... }
sample_rate: 48000
block_size: 128
buffer_size: 0          # 实时异步环形缓冲(帧)；0=自动(约100ms)
tasks:
  - { id: default, name: Default, sample_rate: 48000, block_size: 128, priority: 0 }
graph:
  nodes:
    - id: gain                    # 节点 id：字母数字下划线
      component: orpheus.builtin.gain   # 或 sub:<子组件id>
      task: default
      params: { gain_db: -6.0, channels: 2 }
      position: { x: 300, y: 100 }      # UI 画布坐标
  connections:
    - { from: "wav_in:out", to: "gain:in" }   # "节点:端口" 字符串
```

校验：`orpheus_core/schemas/project.schema.json`（jsonschema draft-07）。解析入口 `ProjectLoader`。

工程级全局参数：`sample_rate`/`block_size` 为图形编译期采样率与调度量子（整图单一，由 task 决定）；`buffer_size` 为实时异步环形缓冲容量（帧，0=自动约 100ms）。device_in/device_out 可在节点 params 声明 `sample_rate`（0=继承工程默认）覆盖工程采样率——编译期采用为图形采样率，运行时 rt_host 按设备 nativeDataFormats 校验（不支持报错、需转换告警）。block_size 不按源配置（设备周期已与 block_size 解耦）。UI「⚙ 设置」编辑这三项。

## 子组件（subcomponents）

工程私有复合组件，内嵌在工程文档顶层：

```yaml
subcomponents:
  - id: chain
    name: 链路子组件
    ports:                        # 对外接口；maps_to 必须指向内部【原子】节点端口
      - { id: in,  direction: input,  maps_to: "gain:in" }
      - { id: out, direction: output, maps_to: "biquad:out" }
    graph: { nodes: [...], connections: [...] }   # 与主图同格式
```

- 主图引用：`component: "sub:chain"`；可多实例、可嵌套（内层可含其他 sub: 实例）
- 编译前 `flatten_project()` 递归展开：内部节点 id 加 `<实例>__` 前缀，边界按 maps_to 重接
- 校验错误（均为 CompileError → API 400）：引用未定义、循环引用、maps_to 指向不存在或非原子节点、端口 id 重复、实例连接不存在的端口
- v1 限制：无参数提升（mask）；maps_to 不允许指向内层子组件实例
- UI：框选节点→「包装为子组件」自动推导边界端口；双击实例开独立标签页编辑

## workspace 布局

```
workspace/<工程名>/
  project.yaml          # 唯一事实来源（写穿持久化）
  project.plan.json     # 编译产物（可再生成）
  outputs/              # 运行产物（输出 wav 等）
  generated/            # 「编译后运行」生成的独立 C 工程
  test_input.wav        # 上传/导入的输入文件
```

`workspace/` 已 gitignore。`GET /api/projects/{name}/download` 打包 zip。

## 路径规则

- 工程内 `file_path` 参数一律**相对工程目录**（运行子进程 cwd=工程目录）；导入示例时后端自动把绝对路径改写为相对路径并拷贝输入文件（依据：节点有入连接无出连接=输出型→outputs/，否则=输入型→拷入）
- 组件 DLL：`build/components/lib<target>.dll`（target = 组件 id 点换下划线）；加载必须绝对路径
- 生成工程：ABI 头文件已 vendor 到 `generated/include/`，可脱离仓库编译；构建时显式传主构建的 `CMAKE_C_COMPILER`
