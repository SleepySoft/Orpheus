# 共用专业绘图组件设计（Plot Widget）

> 状态：设计草案 v0.1。目标：为波形、频谱、响应曲线、控制曲线、热力图等监控显示提供统一、清晰、可扩展的绘图基础组件，避免每个节点组件重复实现一套 Canvas 逻辑。

## 1. 问题

当前 `ui/src/nodeWidgets.js` 中的绘图组件各自维护 Canvas、网格、坐标标签、颜色和绘制逻辑。它们不是共享组件，导致：

- 高分屏和 React Flow 放大时线条、文字模糊；
- 没有统一的坐标轴、刻度、单位、图例和十字光标；
- 新增绘图节点需要重复处理缩放、边距、坐标映射和刷新；
- 小节点、大节点、详情面板没有统一的外观规范。

现有 Canvas 数据本身是实时绘制的位图，不是静态图片。模糊主要来自：Canvas 的 backing store 没有乘 `devicePixelRatio`，且 React Flow 的 zoom transform 会继续放大位图。

## 2. 目标

- 所有绘图组件共用同一个绘图引擎和视觉规范。
- 在高分屏和画布缩放后保持线条与文字清晰。
- 提供专业坐标轴：轴线、主刻度、次网格、单位、标签、范围和格式化。
- 支持波形包络、折线、面积、柱状、散点和热力图。
- 支持十字光标、最近数据提示和图例。
- 支持节点紧凑模式和详情/大节点完整模式。
- 不引入重量级图表库，避免和现有节点视觉、性能和打包体积冲突。

## 3. 非目标

- 第一阶段不追求完整 DAW 级别的多轨编辑器。
- 不做音频样本级交互剪辑。
- 不改变后端 ABI、probe 协议和 `project.yaml` 格式。
- 第一阶段不引入 WebGL；如果后续出现超大波形再增加 WebGL backend。

## 4. 方案总览

新增一个绘图模块，按“数据适配 + 通用渲染 + 组件封装”分层：

```text
ui/src/plot/
  Plot.js                 顶层声明式组件，负责布局、图例、光标和交互
  PlotCanvas.js           Canvas 渲染器，负责坐标映射、序列绘制和轴线
  plotScales.js           线性、对数、dB、时间刻度和格式化
  usePlotCanvasSize.js    尺寸、devicePixelRatio、React Flow zoom 适配
  plotAdapters.js         probe 数据到 series 的标准适配器
```

组件层级：

```text
nodeWidgets.js
  ScopeWidget        -> <Plot series={[waveformSeries(...)]} />
  SpectrumWidget     -> <Plot series={[spectrumSeries(...)]} />
  SweepPlotWidget    -> <Plot series={[sweepSeries(...)]} />
  TimeCurveWidget    -> <Plot series={[timeSeries(...)]} />
  HeatmapWidget      -> <Plot series={[matrixSeries(...)]} />
```

`Plot` 是唯一外部入口。`nodeWidgets.js` 只负责把 `data.probe` 数据转换成标准 `series`，不再直接操作 Canvas。

## 5. 核心 API

第一版 API 保持声明式和轻量：

```jsx
<Plot
  variant="node"            // node | panel | expanded
  series={[...]}
  x={{ label: '时间', unit: 's', scale: 'linear', domain: [0, 0.085] }}
  y={{ label: '幅值', unit: '', scale: 'linear', domain: [-1, 1] }}
  grid="major"              // none | major | minor
  legend={false}
  crosshair={false}
/>
```

标准序列类型：

```js
const series = [
  {
    id: 'waveform',
    label: '输出',
    type: 'waveform',       // waveform | line | area | bars | scatter | heatmap
    data: Float32Array,
    color: '#4cc9f0',
    width: 1.5,
    opacity: 1,
  },
];
```

高频波形不直接绘制每个样本点，而是由渲染器按可视列宽度计算 `min/max` 包络，绘制实心和半透明的包络。这样既能表现波形密度，又避免绘制数万个点导致卡顿。

坐标轴定义：

```js
{
  label: '频率',
  unit: 'Hz',
  scale: 'log',              // linear | log | db | time
  domain: [20, 20000],
  ticks: 'auto',             // auto | number | number[]
  reverse: false,
}
```

`plotScales.js` 负责生成干净刻度，例如：

- 时间：`10 ms`、`50 ms`、`100 ms`；
- 频率：`20 Hz`、`100 Hz`、`1 kHz`、`10 kHz`；
- 幅值：`0.5`、`0`、`-0.5`；
- 能量：`0 dB`、`-20 dB`、`-40 dB`、`-60 dB`。

## 6. 清晰渲染策略

Canvas 尺寸按逻辑 CSS 尺寸 + 实际像素密度计算：

```js
const scale = devicePixelRatio * zoom;
canvas.style.width = `${cssWidth}px`;
canvas.style.height = `${cssHeight}px`;
canvas.width = Math.max(1, Math.round(cssWidth * scale));
canvas.height = Math.max(1, Math.round(cssHeight * scale));

ctx.setTransform(scale, 0, 0, scale, 0, 0);
```

其中 `zoom` 来自 React Flow viewport。这样画布虽然仍按 CSS 尺寸参与 React Flow transform，但 backing store 已经包含 zoom 像素密度，浏览器不需要继续拉伸低分辨率位图。

渲染规则：

- 1px 网格线对齐半像素：`Math.round(x) + 0.5`。
- 线宽在 `node` 模式使用 1 到 1.5px，在 `panel/expanded` 模式使用 1.5 到 2px。
- 文本按 `devicePixelRatio * zoom` 渲染，而不是先用 10px 渲染再放大。
- 对极端 zoom 或超大 Canvas 设置 backing store 上限，避免内存异常；超过上限时按比例降采样并记录降采样状态。

## 7. 视觉布局

绘图区使用固定边距，保证不同组件对齐：

```text
┌────────────────────────────────────────────────────────────┐
│ plot title / legend                                        │
│ Y label ┌────────────────────────────────────────────┐     │
│         │                                            │     │
│         │                   plot area                │     │
│         │                                            │     │
│         └────────────────────────────────────────────┘     │
│                    X label                                 │
└────────────────────────────────────────────────────────────┘
```

三种模式：

| 模式 | 用途 | 显示 |
|---|---|---|
| `node` | 画布小节点 | 简化网格、少量刻度、无图例 |
| `panel` | 大节点/参数面板 | 完整网格、单位、图例、光标 |
| `expanded` | 展开视图 | 完整网格、图例、光标、可选缩放/重置 |

节点小图不能牺牲可读性。默认保留左右少量边距和至少 2 到 3 个主刻度，文字尺寸不小于 10 CSS px。

## 8. 交互

第一版交互：

- 指针悬停显示十字光标；
- 显示最近序列点的 x/y 值；
- `panel/expanded` 模式显示图例；
- `expanded` 模式支持框选放大、双击重置。

后续交互：

- 单序列显示/隐藏；
- 手动锁定 Y 轴范围；
- 导出 PNG/CSV；
- 多通道对比模式；
- 横向时间窗拖动。

Canvas 上方的 tooltip 和光标状态由 DOM 层处理，数据绘制仍走 Canvas，保证性能和视觉统一。

## 9. probe 数据适配

`plotAdapters.js` 集中转换现有数据：

| 组件 | 输入 probe | 输出 series |
|---|---|---|
| `probe_waveform` | `waveform: number[]` | `type: 'waveform'` |
| `probe_spectrum` | `spectrum: number[]` | `type: 'bars'`，Y 轴 dB |
| `psd` | `spectrum: number[]` | `type: 'bars'` 或 `line` |
| `sweep_record` | `freq/mag` | `type: 'line'`，X 轴 log，Y 轴 dB |
| `interp_lut` | `history: number[]` | `type: 'line'` |
| `coherence_matrix` | `coherence.matrix` | `type: 'heatmap'` |

适配器还应推导默认轴：

- 波形：时间轴 + 幅值轴；
- 频谱/PSD：Hz 轴 + dB 轴；
- 扫频：log Hz 轴 + dB 轴；
- 控制历史：时间/帧轴 + 数值轴；
- 矩阵：通道索引 X/Y 轴 + 颜色轴。

如果 probe 中没有采样率、窗长或时间信息，由组件从节点 `rate`、参数或编译元数据传入；适配器不猜测。

## 10. 性能策略

- `series` 数据或尺寸变化时才重绘。
- probe 轮询频率高于绘制频率时使用 `requestAnimationFrame` 合并重绘。
- `waveform` 默认做列包络，不逐点绘制。
- 超过阈值的大数组先做浏览器端降采样。
- `Heatmap` 支持离屏 Canvas 缓存，只有矩阵内容变化时才重建。

## 11. 迁移计划

### 阶段 1：基础组件

- 新增 `Plot`、`PlotCanvas`、`plotScales`、`usePlotCanvasSize`。
- 实现线性/对数/dB 刻度、坐标轴、网格、图例和十字光标。
- 提供 `line`、`area`、`bars` 序列。

### 阶段 2：迁移曲线类组件

- 迁移 `SweepPlotWidget`。
- 迁移 `TimeCurveWidget`。
- 建立清晰度和轴渲染的基线。

### 阶段 3：迁移高频显示

- 迁移 `ScopeWidget`，增加波形包络绘制。
- 迁移 `SpectrumWidget`。
- 清理 `nodeWidgets.js` 中重复 Canvas 逻辑。

### 阶段 4：特殊图表

- 迁移 `HeatmapWidget`。
- 增加热力图颜色轴和矩阵轴。
- 视需要增加散点图。

## 12. 测试

纯函数测试：

- 刻度生成；
- 线性/对数/dB 映射；
- domain 推导；
- 波形包络降采样；
- `devicePixelRatio * zoom` 尺寸计算。

组件测试：

- `Plot` 接收合法/非法 `series`；
- 空数据显示统一占位；
- legend/crosshair 开关；
- 小节点和大面板使用不同边距。

Playwright 验证：

- 打开包含波形、频谱和扫频组件的示例；
- 断言 Canvas backing store 随 zoom 增大；
- 缩放画布后文字和线条保持清晰。

## 13. 验收标准

- 新增一个绘图节点不再需要手写 Canvas。
- 所有曲线/波形节点共享同一套轴、网格、颜色和光标风格。
- 在 Windows 100%/125%/150% DPI 和 React Flow 放大后，文字和线条不出现明显模糊。
- 波形、频谱、扫频、控制曲线都能从同一组件渲染。
- 后端 probe 协议保持不变。
