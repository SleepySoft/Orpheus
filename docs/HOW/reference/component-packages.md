# 统一组件包与目录工程

## 设计原则

物理目录不表达图的逻辑层次。图只引用组件 ID，Resolver 负责从工程 import、全局组件库或二进制包中找到定义。

以下节点写法对原子源码、预编译二进制和递归复合组件完全相同：

```yaml
component: external_model.spatial.fdp
```

`sub:` 仅作为旧工程和 UI 内部视图键兼容，不属于新工程持久化格式。

## 目录工程

```text
my_project/
  project.yaml
  project.layout.yaml
  definitions/
    signal_path.component.yaml
    signal_path.component.layout.yaml
    algorithms/
      fdp.component.yaml
  native/
    coefficient_solver/
      component.yaml
      CMakeLists.txt
      src/
      include/
  assets/
    fdp_coefficients.f32
  notes.md
  node-notes.json
```

目录名称和图层次没有关系；移动定义文件只需更新 `imports`。

```yaml
version: "1.0.0"
imports:
  - definitions/signal_path.component.yaml
  - native/coefficient_solver/component.yaml
  - component: company.shared.calibration
    version: 1.2.0
    sha256: 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef

graph:
  nodes:
    - id: path
      component: company.product.signal_path
    - id: solver
      component: company.algorithm.coefficient_solver
```

字符串 import 是相对 `project.yaml` 或当前组件定义文件的文件/目录。`{component: id}` 引用已安装的全局定义。相对路径不得逃出工程目录；符号链接在 containment 检查前解析。

## 组件库显示顺序

组件库顶部依次显示“工程子组件”和“工程定制组件”，默认收起，可点击展开并查看数量；
之后按[通用组件库](component-library.md)的九个功能类显示。工程导入组件仍可保留多级分类。
工程子组件只列一次，不同时混入分类树；导入的只读子组件不提供删除入口。
工程定制组件包括工程私有导入的原子组件，以及尚未提升为公共库的用户自定义组件。
搜索跨三个范围查找，收起状态不影响搜索结果；拖拽使用原组件ID，不改变工程定义。

`GET /api/projects/{name}/components` 用 `project_local` 标记工程私有定义：
以解析后的manifest来源与全局库比较，不靠ID前缀或分类名猜测；工程registry中的全局快照仍属通用组件。
工程私有组件不提供全局删除/提升操作，用户全局自定义组件原有管理入口不变。

## 统一组件定义

所有定义使用组件 manifest。实现差异只体现在 `package_type`：

```yaml
id: company.product.signal_path
name: 信号链
category: 工程/信号链
version: 1.0.0
abi_version: 1
package_type: composite # source | binary | composite
```

### 复合实现

```yaml
ports:
  - id: in
    direction: input
    type: audio
    channels: 2
    maps_to: filter:in
  - id: out
    direction: output
    type: audio
    channels: 2
    maps_to: gain:out

parameters:
  - id: gain_db
    name: 增益
    type: float
    direction: input
    maps_to: gain:gain_db
    default: 0.0
    update_policy: smoothed

graph:
  nodes:
    - {id: filter, component: orpheus.builtin.biquad, params: {channels: 2}}
    - {id: gain, component: orpheus.builtin.gain, params: {channels: 2}}
  connections:
    - {from: filter:out, to: gain:in}
```

复合组件可以引用另一个复合组件。边界只能映射到直接子实例，但 Resolver 会递归穿透到原子节点。循环引用在 flatten 阶段报错。

### 源码与二进制实现

源码组件沿用 `sources`、`headers`、`deps` 和 `CMakeLists.txt`。工程 import 的源码目录通过 `ORPHEUS_EXTRA_COMPONENT_DIRS` 加入现有构建，目标名称仍为组件 ID 点号替换为下划线。动态运行和代码生成使用同一份项目 Registry。

二进制组件沿用 `binaries[]`。对父图而言，两者与复合组件没有区别。

## 外部资源

大系数不写入 YAML：

```yaml
resources:
  reference.headrest.fir:
    file: assets/headrest.f32
    format: f32le
    shape: [10, 1280]
    sha256: 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef

graph:
  nodes:
    - id: fir
      component: orpheus.builtin.fir
      params:
        channels: 10
        coefficients: {$resource: reference.headrest.fir, offset: 0, count: 1280}
```

支持 `f32le`、`json`、`csv`。加载时校验 hash 和 shape，再转换为现有 BULK 参数；序列化时仍保留 `$resource`。因此动态路径和代码生成路径继续消费同一个 plan 数值。

参数引用可带 `offset/count`，让多个基础组件共享一个大资源文件而不复制数据。

## 加载与保存

1. 加载全局 Registry 快照。
2. 从 `project.yaml` 递归解析 imports。
3. 检测重复组件 ID、路径逃逸、缺失定义、全局版本与 manifest 哈希。
4. 将复合定义物化为内存图。
5. flatten 为原子 Project 后交给现有 Compiler。

API GET 会物化复合定义供 UI 编辑；PUT 按组件来源写回各自 manifest。新建的带命名空间组件自动保存到 `components/<id>.component.yaml`，不会回填进 `project.yaml`。

### 结构与画布布局

加载支持两种位置来源：原有节点/访问桥的内嵌 `position`，以及独立布局文件。**今后保存默认使用独立布局文件**；旧工程在下次保存时迁出坐标，不需要提前批量迁移。

结构文件仅保留布局引用：

```yaml
layout:
  file: project.layout.yaml
```

布局文件以视图和节点 ID 定位，不影响连线、参数、调用顺序或执行计划：

```yaml
version: 1
views:
  main:
    nodes:
      source: {x: 40, y: 80}
    bridges:
      device: {x: 400, y: 80}
  sub:legacy:
    nodes:
      filter: {x: 120, y: 60}
```

- 默认命名为 `project.layout.yaml`、`foo.component.layout.yaml` 或 `component.layout.yaml`，与所属结构文件并置；也可以通过 `layout.file` 指定所属目录内的相对路径，不允许越界或覆盖结构文件。
- 可写复合组件的默认布局写入自身旁文件，父工程和嵌套工程共享；只读/公共组件的位置调整写入当前工程布局的 `sub:<组件ID>` 视图，不修改公共定义。
- 外部布局或显式内嵌 `layout: {version: 1, views: ...}` 优先于旧 `position`，未列出的节点回退到原位置；空坐标 `{}` 表示清空位置。布局中不存在的节点、访问桥或子图视图直接忽略，不会新增对象，也不会导致加载失败；下次保存时清理这些失效记录。
- 内存/API 继续提供节点 `position`，UI 无需感知存储格式。缓存监控结构及布局文件，外部修改布局后重新读取即可生效；引用丢失、格式错误或非有限坐标会明确报错，不静默忽略。
- 首次迁移可能规范化结构字段；之后仅移动节点时，结构 YAML 的字节、注释和修改时间保持不变，组件 manifest 的结构哈希也不随拖动改变。图连线、驱动连接、任务、参数和说明等仍属于结构。
- Python `ProjectLoader.save(..., separate_layout=False)` 可明确选择内嵌保存。完整工程 ZIP 包含布局旁文件，复制工程时应一起保留。

## 不变边界

- Runtime、C ABI 和 execution plan 不感知复合组件。
- CodeGenerator 仍只处理 flatten 后的原子图。
- 节点展开 ID 继续使用 `instance__child`，保持参数 ID 和 arena 布局稳定。
- 工程私有 Registry 与其他工程隔离；重复 ID 不允许隐式覆盖。