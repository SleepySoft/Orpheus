import {
  DRIVE_IN, DRIVE_OUT, BRIDGE_COMPONENT, copyFlowSelection, pasteFlowSelection,
  docToViews, viewsToDoc, driveHandles, flowToGraph, validateDriveConnection,
  driveTimingLabel, reconnectDriveEdge,
} from './graphUtils';

const catalog = [
  { id: 'test.clock', clock_source: true, ports: [{ id: 'out', direction: 'output' }] },
  { id: 'test.gain', ports: [{ id: 'in', direction: 'input' }, { id: 'out', direction: 'output' }] },
];
const document = {
  flow_mode: 'task_sequence',
  graph: {
    nodes: [
      { id: 'clock', component: 'test.clock' },
      { id: 'gain', component: 'test.gain' },
      { id: 'sink', component: 'test.gain' },
    ],
    connections: [{ from: 'clock:out', to: 'gain:in' }],
  },
  control_connections: [{ from: 'clock:level', to: 'gain:value' }],
  drive_connections: [{ from: 'clock:out', to: 'gain:in' }, { from: 'gain:out', to: 'sink:in' }],
};
const connection = (source, target) => ({
  source, target, sourceHandle: DRIVE_OUT, targetHandle: DRIVE_IN,
});

describe('任务驱动执行图', () => {
  test('三类连线独立往返，虚拟引脚不写入数据或参数控制列表', () => {
    const { views, subsMeta } = docToViews(document, catalog);
    expect(views.main.edges.filter((edge) => edge.type === 'drive')).toHaveLength(2);
    expect(views.main.edges.find((edge) => edge.type === 'drive')).toMatchObject({
      sourceHandle: DRIVE_OUT, targetHandle: DRIVE_IN,
    });
    const out = viewsToDoc(views, subsMeta, document);
    expect(out.flow_mode).toBe('task_sequence');
    expect(out.drive_connections).toEqual(document.drive_connections);
    expect(out.control_connections).toEqual(document.control_connections);
    expect(out.graph.connections).toEqual(document.graph.connections);
    expect(views.main.nodes[0].data.ports).toEqual(catalog[0].ports);
  });

  test('切换模式不删除任何连线；重新加载后仍可恢复执行顺序', () => {
    const { views, subsMeta } = docToViews(document, catalog);
    const off = viewsToDoc(views, subsMeta, { ...document, flow_mode: 'dataflow' });
    expect(off.drive_connections).toEqual(document.drive_connections);
    expect(off.graph.connections).toEqual(document.graph.connections);
    expect(off.control_connections).toEqual(document.control_connections);
    const restored = docToViews({ ...off, flow_mode: 'task_sequence' }, catalog);
    expect(restored.views.main.edges.filter((edge) => edge.type === 'drive')).toHaveLength(2);
  });

  test('旧工程保持默认形态，不额外写入执行模式或执行连线', () => {
    const legacy = { graph: document.graph };
    const { views, subsMeta } = docToViews(legacy, catalog);
    const out = viewsToDoc(views, subsMeta, legacy);
    expect(out).not.toHaveProperty('flow_mode');
    expect(out).not.toHaveProperty('drive_connections');
    expect(driveHandles(views.main.nodes[0].data)).toEqual([]);
  });

  test('显式删除执行边后清除顶层字段，保留数据与控制边', () => {
    const { views, subsMeta } = docToViews(document, catalog);
    views.main.edges = views.main.edges.filter((edge) => edge.type !== 'drive');
    const out = viewsToDoc(views, subsMeta, document);
    expect(out).not.toHaveProperty('drive_connections');
    expect(out.graph.connections).toEqual(document.graph.connections);
    expect(out.control_connections).toEqual(document.control_connections);
  });

  test('即使边类型标记缺失，虚拟执行引脚也不能进入数据连接', () => {
    expect(flowToGraph([], [connection('clock', 'gain')]).connections).toEqual([]);
  });

  test('源仅有输出，普通原子节点有输入输出，关闭模式不显示执行引脚', () => {
    const { views } = docToViews(document, catalog);
    expect(driveHandles(views.main.nodes[0].data, 'task_sequence')).toEqual([DRIVE_OUT]);
    expect(driveHandles(views.main.nodes[1].data, 'task_sequence')).toEqual([DRIVE_IN, DRIVE_OUT]);
    expect(driveHandles(views.main.nodes[1].data, 'dataflow')).toEqual([]);
  });

  test.each([
    { component: BRIDGE_COMPONENT },
    { component: 'test.config', bridgeConfig: true },
    { component: 'test.placeholder', packageType: 'placeholder' },
    { component: 'test.missing', missing: true },
  ])('非原子或不可执行节点不提供虚拟接口：%j', (data) => {
    expect(driveHandles(data, 'task_sequence')).toEqual([]);
  });

  test.each([
    { component: 'sub:test' },
    { component: 'test.composite', packageType: 'composite' },
  ])('复合组件提供虚拟执行边界：%j', (data) => {
    expect(driveHandles(data, 'task_sequence')).toEqual([DRIVE_IN, DRIVE_OUT]);
  });

  test.each([
    [{}, ''],
    [{ every: 1, phase: 0 }, ''],
    [{ every: 2 }, '每2次·相位1'],
    [{ every: 3, phase: 0 }, '每3次·相位0'],
    [{ every: 2, phase: 1 }, '每2次·相位1'],
  ])('执行节拍标签按默认相位 every-1 展示：%j', (data, label) => {
    expect(driveTimingLabel(data)).toBe(label);
  });

  test('显式节拍字段在往返、复制、重连和节点参数更新后保留', () => {
    const timed = {
      ...document,
      drive_connections: [
        { from: 'clock:out', to: 'gain:in', every: 2, phase: 1 },
        { from: 'gain:out', to: 'sink:in', every: 3 },
      ],
    };
    const { views, subsMeta } = docToViews(timed, catalog);
    expect(viewsToDoc(views, subsMeta, timed).drive_connections).toEqual(timed.drive_connections);
    const clipboard = copyFlowSelection(views.main, ['clock', 'gain', 'sink']);
    const pasted = pasteFlowSelection(views.main, clipboard);
    expect(viewsToDoc({ main: pasted }, subsMeta, timed).drive_connections).toContainEqual({
      from: 'clock_copy:out', to: 'gain_copy:in', every: 2, phase: 1,
    });
    const old = views.main.edges.find((edge) => edge.type === 'drive');
    const replacement = reconnectDriveEdge(old, connection('clock', 'sink'));
    views.main.edges = views.main.edges.map((edge) => edge.id === old.id ? replacement : edge);
    views.main.nodes[1] = {
      ...views.main.nodes[1],
      data: { ...views.main.nodes[1].data, params: { gain: -3 }, ports: [] },
    };
    const out = viewsToDoc(views, subsMeta, timed);
    expect(out.drive_connections).toContainEqual({
      from: 'clock:out', to: 'sink:in', every: 2, phase: 1,
    });
    expect(out.drive_connections[1]).toEqual(timed.drive_connections[1]);
  });

  test('复合定义的 graph.drive_connections 含节拍往返且不泄漏到主图数据边', () => {
    const composite = {
      ...document,
      subcomponents: [{
        id: 'test.chain', ports: [], graph: {
          nodes: [{ id: 'a', component: 'test.gain' }, { id: 'b', component: 'test.gain' }],
          connections: [{ from: 'a:out', to: 'b:in' }],
          drive_connections: [{ from: 'a:out', to: 'b:in', every: 4, phase: 2 }],
        },
      }],
      graph: {
        ...document.graph,
        nodes: [...document.graph.nodes, { id: 'chain', component: 'test.chain' }],
      },
      drive_connections: [{ from: 'clock:out', to: 'chain:in', every: 2 }],
    };
    const { views, subsMeta } = docToViews(composite, catalog);
    const boundary = views.main.nodes.find((node) => node.id === 'chain');
    expect(driveHandles(boundary.data, composite.flow_mode)).toEqual([DRIVE_IN, DRIVE_OUT]);
    expect(views['sub:test.chain'].edges.find((edge) => edge.type === 'drive').data)
      .toEqual({ every: 4, phase: 2 });
    const out = viewsToDoc(views, subsMeta, composite);
    expect(out.subcomponents[0].graph.drive_connections)
      .toEqual(composite.subcomponents[0].graph.drive_connections);
    expect(out.subcomponents[0].graph.connections)
      .toEqual(composite.subcomponents[0].graph.connections);
    expect(out.drive_connections).toEqual(composite.drive_connections);
    views['sub:test.chain'].edges = [];
    expect(viewsToDoc(views, subsMeta, composite).subcomponents[0].graph)
      .not.toHaveProperty('drive_connections');
  });

  test('接受合法分支，拒绝循环、自环、重复、混连和连接到时钟源', () => {
    const { views } = docToViews(document, catalog);
    const { nodes, edges } = views.main;
    expect(validateDriveConnection(nodes, edges, connection('clock', 'sink'), 'task_sequence')).toBeNull();
    expect(validateDriveConnection(nodes, edges, connection('sink', 'gain'), 'task_sequence')).toMatch('循环');
    expect(validateDriveConnection(nodes, edges, connection('sink', 'sink'), 'task_sequence')).toMatch('循环');
    expect(validateDriveConnection(nodes, edges, connection('clock', 'gain'), 'task_sequence')).toMatch('已存在');
    expect(validateDriveConnection(nodes, edges, connection('gain', 'clock'), 'task_sequence')).toMatch('时钟源');
    expect(validateDriveConnection(nodes, edges, {
      ...connection('clock', 'sink'), targetHandle: 'in',
    }, 'task_sequence')).toMatch('混连');
    expect(validateDriveConnection(nodes, edges, connection('clock', 'sink'), 'dataflow')).not.toBeNull();
  });

  test('复制粘贴保持三类内部边并重映射节点；不复制跨边界连线', () => {
    const { views, subsMeta } = docToViews(document, catalog);
    const clipboard = copyFlowSelection(views.main, ['clock', 'gain']);
    expect(clipboard.edges).toHaveLength(3);
    const pasted = pasteFlowSelection(views.main, clipboard);
    const out = viewsToDoc({ main: pasted }, subsMeta, document);
    expect(out.drive_connections).toContainEqual({ from: 'clock_copy:out', to: 'gain_copy:in' });
    expect(out.graph.connections).toContainEqual({ from: 'clock_copy:out', to: 'gain_copy:in' });
    expect(out.control_connections).toContainEqual({ from: 'clock_copy:level', to: 'gain_copy:value' });
    expect(out.drive_connections).toHaveLength(3);
    const pastedAgain = pasteFlowSelection(pasted, clipboard);
    expect(new Set(pastedAgain.nodes.map((node) => node.id)).size).toBe(pastedAgain.nodes.length);
    expect(new Set(pastedAgain.edges.map((edge) => edge.id)).size).toBe(pastedAgain.edges.length);
    expect(clipboard.nodes[0].id).toBe('clock');
  });
});
