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
  definitions/
    signal_path.component.yaml
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

## 不变边界

- Runtime、C ABI 和 execution plan 不感知复合组件。
- CodeGenerator 仍只处理 flatten 后的原子图。
- 节点展开 ID 继续使用 `instance__child`，保持参数 ID 和 arena 布局稳定。
- 工程私有 Registry 与其他工程隔离；重复 ID 不允许隐式覆盖。