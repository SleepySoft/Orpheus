/** graphUtils 纯函数测试：形状求值/比较、控制 handle、doc↔views 往返（含 control_connections）。 */

import {
  CTL_PREFIX,
  ctlParamId,
  docToViews,
  isControlHandle,
  resolveShape,
  shapeEquals,
  shapeText,
  viewsToDoc,
} from './graphUtils';

// 最小组件目录：gain（bindable gain_db）/ level_detect（control_source level）/ matrix（shape 表达式）
const catalog = [
  {
    id: 'orpheus.builtin.gain',
    name: '增益',
    ports: [
      { id: 'in', direction: 'input', type: 'audio' },
      { id: 'out', direction: 'output', type: 'audio' },
    ],
    parameters: [
      { id: 'gain_db', type: 'float', default: 0.0, bindable: true },
      { id: 'channels', type: 'int', default: 2 },
    ],
  },
  {
    id: 'orpheus.builtin.level_detect',
    name: '电平检测',
    ports: [
      { id: 'in', direction: 'input', type: 'audio' },
      { id: 'out', direction: 'output', type: 'audio' },
    ],
    parameters: [
      { id: 'level', type: 'float', default: 0.0, readback: true, control_source: true },
      { id: 'channels', type: 'int', default: 2 },
    ],
  },
  {
    id: 'orpheus.builtin.matrix_mul',
    name: '矩阵乘法',
    ports: [
      { id: 'in', direction: 'input', type: 'audio' },
      { id: 'out', direction: 'output', type: 'audio' },
    ],
    parameters: [
      { id: 'rows', type: 'int', default: 2 },
      { id: 'cols', type: 'int', default: 2 },
      { id: 'matrix', type: 'string', default: '1, 0, 0, 1', shape: ['param:rows', 'param:cols'] },
    ],
  },
  {
    id: 'orpheus.builtin.access_bridge',
    name: '访问桥',
    bridge_config: true,
    ports: [],
    parameters: [
      { id: 'transport', type: 'string', default: 'uart' },
      { id: 'codec', type: 'string', default: 'olink' },
      { id: 'enabled', type: 'bool', default: true },
      { id: 'resource', type: 'string', default: '' },
      { id: 'baud', type: 'int', default: 921600 },
      { id: 'probe_interval_ms', type: 'float', default: 200 },
    ],
  },
  {
    id: 'orpheus.builtin.uart_link',
    name: '串口链路',
    ports: [],
    parameters: [
      { id: 'link_name', type: 'string', default: '' },
      { id: 'baud', type: 'int', default: 921600 },
      { id: 'probe_interval_ms', type: 'float', default: 200 },
    ],
  },
];

describe('resolveShape', () => {
  const matrix = catalog[2];

  test('空/缺省声明 = 标量 []', () => {
    expect(resolveShape(undefined, {}, matrix)).toEqual([]);
    expect(resolveShape([], {}, matrix)).toEqual([]);
  });

  test('整数常量直接通过', () => {
    expect(resolveShape([4], {}, matrix)).toEqual([4]);
  });

  test('param: 引用按节点参数求值', () => {
    expect(resolveShape(matrix.parameters[2].shape, { rows: 2, cols: 3 }, matrix)).toEqual([2, 3]);
  });

  test('节点未给值时回退 manifest 默认值', () => {
    expect(resolveShape(matrix.parameters[2].shape, {}, matrix)).toEqual([2, 2]);
  });

  test('求值失败（未知参数且无默认值）返回 null', () => {
    expect(resolveShape(['param:nope'], {}, matrix)).toBeNull();
    expect(resolveShape(['abc'], {}, matrix)).toBeNull();
  });
});

describe('shapeText / shapeEquals', () => {
  test('标量 ·、数组 [n]、矩阵 [r×c]、未知 ?', () => {
    expect(shapeText([])).toBe('·');
    expect(shapeText([2])).toBe('[2]');
    expect(shapeText([2, 2])).toBe('[2×2]');
    expect(shapeText(null)).toBe('?');
    expect(shapeText(undefined)).toBe('?');
  });

  test('严格相等（null 不相等）', () => {
    expect(shapeEquals([], [])).toBe(true);
    expect(shapeEquals([2, 2], [2, 2])).toBe(true);
    expect(shapeEquals([2], [2, 2])).toBe(false);
    expect(shapeEquals([], [1])).toBe(false);
    expect(shapeEquals(null, [])).toBe(false);
    expect(shapeEquals(null, null)).toBe(false);
  });
});

describe('控制 handle 工具', () => {
  test('isControlHandle / ctlParamId', () => {
    expect(isControlHandle(`${CTL_PREFIX}gain_db`)).toBe(true);
    expect(isControlHandle('in')).toBe(false);
    expect(isControlHandle(undefined)).toBe(false);
    expect(ctlParamId(`${CTL_PREFIX}gain_db`)).toBe('gain_db');
    expect(ctlParamId('in')).toBe('in');
  });
});

describe('docToViews → viewsToDoc 往返（含 control_connections）', () => {
  const doc = {
    version: '0.1.0',
    metadata: { name: 't' },
    graph: {
      nodes: [
        { id: 'lvl', component: 'orpheus.builtin.level_detect', params: { channels: 2 } },
        { id: 'g', component: 'orpheus.builtin.gain', params: { channels: 2 } },
      ],
      connections: [{ from: 'lvl:out', to: 'g:in' }],
    },
    control_connections: [{ from: 'lvl:level', to: 'g:gain_db' }],
  };

  test('音频边与控制边同时进 views，控制边 id 前缀 ec-、handle 带 ctl:', () => {
    const { views } = docToViews(doc, catalog);
    const edges = views.main.edges;
    expect(edges).toHaveLength(2);
    const audio = edges.find((e) => e.type !== 'control');
    const ctlEdge = edges.find((e) => e.type === 'control');
    expect(audio.id).toBe('e-lvl:out-g:in');
    expect(ctlEdge.id).toBe('ec-lvl:level-g:gain_db');
    expect(ctlEdge.sourceHandle).toBe('ctl:level');
    expect(ctlEdge.targetHandle).toBe('ctl:gain_db');
  });

    test('节点 Task 归属往返保持不变', () => {
      const taskDoc = {
        ...doc,
        graph: {
          ...doc.graph,
          nodes: doc.graph.nodes.map((node, index) => ({
            ...node,
            task: index === 0 ? 'producer' : 'consumer',
          })),
        },
      };
      const { views, subsMeta } = docToViews(taskDoc, catalog);
      const out = viewsToDoc(views, subsMeta, taskDoc);
      expect(out.graph.nodes.map((node) => node.task)).toEqual(['producer', 'consumer']);
    });

  test('写回：控制边剥 ctl: 前缀回 control_connections，音频边进 connections', () => {
    const { views, subsMeta } = docToViews(doc, catalog);
    const out = viewsToDoc(views, subsMeta, doc);
    expect(out.graph.connections).toEqual([{ from: 'lvl:out', to: 'g:in' }]);
    expect(out.control_connections).toEqual([{ from: 'lvl:level', to: 'g:gain_db' }]);
  });

  test('无控制边时省略 control_connections 段', () => {
    const noCtl = {
      ...doc,
      control_connections: undefined,
    };
    const { views, subsMeta } = docToViews(noCtl, catalog);
    expect(views.main.edges.every((e) => e.type !== 'control')).toBe(true);
    const out = viewsToDoc(views, subsMeta, noCtl);
    expect('control_connections' in out).toBe(false);
  });

  test('子组件公开参数成为控制 handle 并无损往返', () => {
    const subDoc = {
      ...doc,
      graph: {
        nodes: [{ id: 'chain1', component: 'sub:chain', params: { gain: -12 } }],
        connections: [],
      },
      control_connections: [{ from: 'chain1:level', to: 'chain1:gain' }],
      subcomponents: [{
        id: 'chain',
        ports: [],
        public_parameters: [
          { id: 'level', direction: 'output', maps_to: 'meter:level', type: 'float' },
          { id: 'gain', direction: 'input', maps_to: 'gain:gain_db', type: 'float', default: -6 },
        ],
        graph: { nodes: [], connections: [] },
      }],
    };
    const { views, subsMeta } = docToViews(subDoc, catalog);
    const params = views.main.nodes[0].data.parameters;
    expect(params.find((p) => p.id === 'level').control_source).toBe(true);
    expect(params.find((p) => p.id === 'gain').bindable).toBe(true);
    const out = viewsToDoc(views, subsMeta, subDoc);
    expect(out.subcomponents[0].public_parameters).toEqual(subDoc.subcomponents[0].public_parameters);
  });

  test('顶层 bridges 投影为无端口节点并保存回顶层', () => {
    const bridgeDoc = {
      ...doc,
      graph: { nodes: [], connections: [] },
      bridges: [{
        id: 'debug_uart', transport: 'uart', codec: 'olink', enabled: true,
        params: { resource: 'uart2', baud: 115200, probe_interval_ms: 50 },
        position: { x: 40, y: 80 },
      }],
    };
    const { views, subsMeta } = docToViews(bridgeDoc, catalog);
    const node = views.main.nodes[0];
    expect(node.id).toBe('debug_uart');
    expect(node.data.component).toBe('orpheus.builtin.access_bridge');
    expect(node.data.ports).toEqual([]);
    expect(node.data.params.transport).toBe('uart');
    const out = viewsToDoc(views, subsMeta, bridgeDoc);
    expect(out.graph.nodes).toEqual([]);
    expect(out.bridges).toEqual(bridgeDoc.bridges);
  });

  test('旧 uart_link 节点首次保存迁移到 bridges', () => {
    const legacy = {
      ...doc,
      graph: {
        nodes: [{
          id: 'legacy_link', component: 'orpheus.builtin.uart_link',
          params: { link_name: 'uart0', baud: 921600, probe_interval_ms: 100 },
          position: { x: 20, y: 30 },
        }],
        connections: [],
      },
    };
    const { views, subsMeta } = docToViews(legacy, catalog);
    const out = viewsToDoc(views, subsMeta, legacy);
    expect(out.graph.nodes).toEqual([]);
    expect(out.bridges[0]).toMatchObject({
      id: 'legacy_link', transport: 'uart', codec: 'olink', enabled: true,
      params: { link_name: 'uart0', baud: 921600, probe_interval_ms: 100 },
      position: { x: 20, y: 30 },
    });
  });
});
