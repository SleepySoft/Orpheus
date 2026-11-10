import {
  DRIVE_IN, DRIVE_OUT, BRIDGE_COMPONENT, copyFlowSelection, pasteFlowSelection,
  docToViews, viewsToDoc, driveHandles, flowToGraph, validateDriveConnection,
  driveTimingLabel, reconnectDriveEdge,
  driveMemberships, driveMembershipsForViews, resolvedDriveEdges,
  subCatalogEntry, resolvePorts,
  projectDriveEdgesForView,
  removeFlowEdgesForNodes,
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

describe('跨层源入口的实际归属与折叠投影', () => {
  const typedCatalog = [
    { id: 'test.clock', clock_source: true, ports: [] },
    { id: 'test.gain', ports: [
      { id: 'in', direction: 'input', type: 'audio' },
      { id: 'out', direction: 'output', type: 'audio' },
    ] },
    { id: 'test.buffer', ports: [
      { id: 'in', direction: 'input', type: 'audio', propagate_drive: false },
      { id: 'out', direction: 'output', type: 'audio' },
    ] },
  ];
  const deepDocument = {
    flow_mode: 'task_sequence',
    graph: {
      nodes: [
        { id: 'clock', component: 'test.clock' },
        { id: 'wrapper', component: 'test.outer' },
        { id: 'sink', component: 'test.gain' },
      ],
      connections: [{ from: 'wrapper:out', to: 'sink:in' }],
    },
    subcomponents: [
      {
        id: 'test.outer', ports: [
          { id: 'in', direction: 'input', type: 'audio', maps_to: 'inner:in' },
          { id: 'out', direction: 'output', type: 'audio', maps_to: 'inner:out' },
        ],
        graph: { nodes: [{ id: 'inner', component: 'test.inner' }], connections: [] },
      },
      {
        id: 'test.inner', ports: [
          { id: 'in', direction: 'input', type: 'audio', maps_to: 'a:in' },
          { id: 'out', direction: 'output', type: 'audio', maps_to: 'b:out' },
        ],
        graph: {
          nodes: [{ id: 'a', component: 'test.gain' }, { id: 'b', component: 'test.buffer' }],
          connections: [{ from: 'a:out', to: 'b:in' }],
        },
      },
    ],
    drive_connections: [
      { from: 'clock:out', to: 'wrapper__inner__a:in', every: 1, phase: 0 },
      { from: 'clock:out', to: 'wrapper__inner__b:in', every: 13, phase: 12 },
    ],
  };

  test('按完整实例入口计算内部周期，父层仅汇总，不把全部孩子染成父层周期', () => {
    const { views } = docToViews(deepDocument, typedCatalog);
    const marks = driveMembershipsForViews(views, 'task_sequence');
    expect(marks['sub:test.inner'].a.map((domain) => domain.every)).toEqual([1]);
    expect(marks['sub:test.inner'].b.map((domain) => domain.every)).toEqual([13]);
    expect(marks.main.sink.map((domain) => domain.every)).toEqual([13]);
    expect(new Set(marks.main.wrapper.map((domain) => domain.every))).toEqual(new Set([1, 13]));
  });

  test('公开输出截断在多层映射后仍生效', () => {
    const doc = JSON.parse(JSON.stringify(deepDocument));
    doc.subcomponents[0].ports[1].propagate_drive = false;
    const { views } = docToViews(doc, typedCatalog);
    const marks = driveMembershipsForViews(views, 'task_sequence');
    expect(marks.main.sink).toEqual([]);
    expect(marks['sub:test.inner'].b).toHaveLength(1);
  });

  test('已有内部入口时明确拒绝重复启动整个折叠模块', () => {
    const { views } = docToViews(deepDocument, typedCatalog);
    expect(validateDriveConnection(views.main.nodes, views.main.edges,
      connection('clock', 'wrapper'), 'task_sequence')).toContain('已有内部驱动入口');
  });

  test('复制粘贴重映射跨层实例前缀，删除模块同时清除其内部入口', () => {
    const { views } = docToViews(deepDocument, typedCatalog);
    const clipboard = copyFlowSelection(views.main, ['clock', 'wrapper']);
    expect(clipboard.edges.filter((edge) => edge.type === 'drive')).toHaveLength(2);
    const pasted = pasteFlowSelection(views.main, clipboard);
    expect(flowToGraph(pasted.nodes, pasted.edges).connections).toEqual(deepDocument.graph.connections);
    expect(pasted.edges.filter((edge) => edge.source === 'clock_copy').map((edge) => edge.target))
      .toEqual(['wrapper_copy__inner__a', 'wrapper_copy__inner__b']);
    const remaining = removeFlowEdgesForNodes(views.main.nodes, views.main.edges, ['wrapper']);
    expect(remaining).toEqual([]);
  });

  test('折叠入口按周期汇总绘制，往返不丢完整引用和原始调用顺序', () => {
    const doc = JSON.parse(JSON.stringify(deepDocument));
    doc.drive_connections.push({
      from: 'clock:out', to: 'wrapper__inner__extra:in', every: 1, phase: 0,
    });
    const { views, subsMeta } = docToViews(doc, typedCatalog);
    const original = JSON.stringify(views.main.edges);
    const projections = projectDriveEdgesForView(views.main.nodes, views.main.edges)
      .filter((edge) => edge.data?.entryRefs);
    expect(projections).toHaveLength(2);
    expect(projections[0]).toMatchObject({
      source: 'clock', target: 'wrapper', selectable: false, deletable: false, updatable: false,
      data: { every: 1, phase: 0, entryRefs: ['wrapper__inner__a', 'wrapper__inner__extra'] },
    });
    expect(JSON.stringify(views.main.edges)).toBe(original);
    expect(viewsToDoc(views, subsMeta, doc).drive_connections).toEqual(doc.drive_connections);
  });
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
    expect(validateDriveConnection(nodes, edges, connection('sink', 'gain'), 'task_sequence')).toMatch('驱动源本体');
    expect(validateDriveConnection(nodes, edges, connection('sink', 'sink'), 'task_sequence')).toMatch('驱动源本体');
    expect(validateDriveConnection(nodes, edges, connection('clock', 'gain'), 'task_sequence')).toMatch('已存在');
    expect(validateDriveConnection(nodes, edges, connection('gain', 'clock'), 'task_sequence')).toMatch('时钟源');
    expect(validateDriveConnection(nodes, edges, {
      ...connection('clock', 'sink'), targetHandle: 'in',
    }, 'task_sequence')).toMatch('混连');
    expect(validateDriveConnection(nodes, edges, connection('clock', 'sink'), 'dataflow')).not.toBeNull();
  });

  describe('声明式传播与驱动域颜色', () => {
    const port = (id, direction, extra = {}) => ({ id, direction, type: 'audio', ...extra });
    const node = (id, clockSource = false) => ({ id, data: { label: id, clockSource,
      ports: [port('in', 'input'), port('out', 'output')] } });
    const wire = (source, target) => ({ source, target, sourceHandle: 'out', targetHandle: 'in' });

    test('普通音频链自动传播，相同入口的颜色一致，不写入派生连线', () => {
      const nodes = [node('source', true), node('a'), node('b')];
      const edges = [wire('source', 'a'), wire('a', 'b')];
      const before = JSON.stringify(edges);
      const marks = driveMemberships(nodes, edges, 'task_sequence');
      expect(marks.a).toEqual(marks.source);
      expect(marks.b).toEqual(marks.source);
      expect(marks.a).toHaveLength(1);
      expect(JSON.stringify(edges)).toBe(before);
      expect(driveMemberships(nodes, edges, 'dataflow').a).toEqual([]);
    });

    test('输出声明false截断，控制数据与参数控制边不传播', () => {
      const nodes = [node('source', true), node('a'), node('b')];
      const edges = [wire('source', 'a'), wire('a', 'b')];
      nodes[1].data.ports[1].propagate_drive = false;
      expect(driveMemberships(nodes, edges, 'task_sequence').b).toEqual([]);
      delete nodes[1].data.ports[1].propagate_drive;
      nodes[1].data.ports[1].type = 'control';
      expect(driveMemberships(nodes, edges, 'task_sequence').b).toEqual([]);
      nodes[1].data.ports[1].propagate_drive = true;
      expect(driveMemberships(nodes, edges, 'task_sequence').b).toHaveLength(1);
      edges[1].type = 'control';
      expect(driveMemberships(nodes, edges, 'task_sequence').b).toEqual([]);
    });

    test('显式消费入口优先，异步输入不继承生产域，消费输出正常续传', () => {
      const nodes = [node('producer', true), node('consumer', true), node('buffer'), node('sink')];
      nodes[2].data.ports[0].propagate_drive = false;
      const edges = [wire('producer', 'buffer'), wire('buffer', 'sink'),
        { ...connection('consumer', 'buffer'), type: 'drive', data: { every: 4, phase: 3 } }];
      const marks = driveMemberships(nodes, edges, 'task_sequence');
      expect(marks.buffer[0].id).toBe('consumer:4:3');
      expect(marks.sink).toEqual(marks.buffer);
      expect(marks.buffer[0].color).not.toBe(marks.producer[0].color);
      expect(resolvedDriveEdges(nodes, edges).filter((edge) => edge.target === 'buffer')).toHaveLength(1);
      edges.pop();
      expect(driveMemberships(nodes, edges, 'task_sequence').buffer).toEqual([]);
    });

    test('多入口用多个点，不依靠数据环无限传播', () => {
      const nodes = [node('first', true), node('second', true), node('a'), node('b')];
      const edges = [wire('a', 'b'), wire('b', 'a'),
        { ...connection('first', 'a'), type: 'drive' },
        { ...connection('second', 'a'), type: 'drive' }];
      expect(driveMemberships(nodes, edges, 'task_sequence').a).toHaveLength(2);
      expect(driveMemberships(nodes, edges, 'task_sequence').b).toHaveLength(2);
    });

    test('展开复合定义仍显示实例继承的驱动域，内部边界继续截断', () => {
      const composite = node('pair');
      composite.data.component = 'sub:pair';
      const views = {
        main: { nodes: [node('source', true), composite], edges: [wire('source', 'pair')] },
        'sub:pair': { nodes: [node('a'), node('b')], edges: [wire('a', 'b')] },
      };
      const marks = driveMembershipsForViews(views, 'task_sequence');
      expect(marks['sub:pair'].a).toEqual(marks.main.pair);
      expect(marks['sub:pair'].b).toEqual(marks.main.pair);
      views['sub:pair'].nodes[0].data.ports[1].propagate_drive = false;
      expect(driveMembershipsForViews(views, 'task_sequence')['sub:pair'].b).toEqual([]);
    });

    test('复合实例端口刷新保留传播声明及内部映射', () => {
      const ports = [{ id: 'out', direction: 'output', maps_to: 'inner:out', propagate_drive: false }];
      expect(resolvePorts(subCatalogEntry({ id: 'pair', ports }), {}))
        .toEqual(ports);
    });
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
