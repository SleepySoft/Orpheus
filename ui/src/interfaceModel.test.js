import React from 'react';
import { createRoot } from 'react-dom/client';
import { act } from 'react';
import ParamPanel from './ParamPanel';
import OrpheusNode from './OrpheusNode';
import SubPortsPanel from './SubPortsPanel';
import ParamBrowser from './ParamBrowser';
import { interfaceState, portSignature, bindingStatus, canEditParameter, canEditDesignParameter, isInitializationParameter } from './interfaceModel';
import { docToViews, docToViewsAsync, viewsToDoc, subCatalogEntry, copyFlowSelection, pasteFlowSelection } from './graphUtils';

jest.mock('./nodeWidgets', () => ({ NODE_WIDGETS: {} }));
jest.mock('reactflow', () => ({
  Handle: ({ id, type, isConnectable }) => <span data-handle={id} data-type={type} data-connectable={String(isConnectable)} />,
  Position: { Left: 'left', Right: 'right', Top: 'top' },
  NodeResizer: () => null, useUpdateNodeInternals: () => jest.fn(),
}));

const ports = [
  { id: 'samples', direction: 'input', type: 'audio', format: 'f32', channels: 2,
    block_size: 8, host_binding: { slot: 'samples', access: 'write', status: 'ready', when: { mode: 0 } } },
  { id: 'out', direction: 'output', type: 'audio', format: 's16', channels: 6, block_size: 'param:frames' },
  { id: 'other', direction: 'output', type: 'audio', format: 'f32', channels: 1, block_size: 'param:frames*2' },
];
const parameters = [
  { id: 'samples', name: '普通配置', type: 'float', default: 2, access: 'write' },
  { id: 'ready', type: 'bool', default: false, access: 'read_write', readback: true },
  { id: 'valid', type: 'bool', default: false, access: 'read' },
  { id: 'missing_blocks', type: 'int', access: 'read' },
  { id: 'legacy', type: 'float', default: 3, readback: true },
  { id: 'level', type: 'float', access: 'read', control_source: true },
];
const catalog = [{ id: 'test.host', ports, parameters, bulk_slots: [{ id: 'samples', type: 'float', access: 'read_write' }] }];
const node = { id: 'host', data: { component: 'test.host', ports, parameters,
  bulkSlots: catalog[0].bulk_slots, params: { mode: 0, frames: 16 }, blockSize: 32 } };

let root;
let container;
beforeEach(() => {
  global.IS_REACT_ACT_ENVIRONMENT = true;
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
});
afterEach(() => { act(() => root.unmount()); container.remove(); });

test('完整声明参数包含 readback/未知旧权限，读权限只读而非隐藏', () => {
  act(() => root.render(<ParamPanel node={node} viewKey="main" onParamChange={jest.fn()} />));
  expect(container.querySelectorAll('[data-interface^="parameter:"]')).toHaveLength(parameters.length);
  for (const id of ['valid', 'missing_blocks', 'level', 'ready']) {
    expect(container.querySelector(`[data-interface="parameter:${id}"] .param-field input`).disabled).toBe(true);
  }
  for (const id of ['samples', 'legacy']) {
    expect(container.querySelector(`[data-interface="parameter:${id}"] .param-field input`).disabled).toBe(false);
  }
  expect(container.querySelector('[data-interface="parameter:legacy"]').textContent).toContain('权限未显式声明');
  expect(container.querySelectorAll('[data-interface^="bulk:"]')).toHaveLength(1);
  expect(container.querySelectorAll('[data-interface^="port:"]')).toHaveLength(3);
  expect(container.textContent).toContain('f32 · 2ch · 8帧');
  expect(container.textContent).toContain('s16 · 6ch · 16帧');
  expect(container.textContent).toContain('未确定 (param:frames*2)');
  expect(container.querySelector('[data-interface="parameter:ready"]').textContent).toContain('由完整 PCM 提交原子更新');
});

test('默认不画接口；同名配置、数据、Buffer 分开显露，不提升配置连接权限', () => {
  act(() => root.render(<OrpheusNode id={node.id} data={node.data} />));
  expect(container.querySelectorAll('[data-handle]')).toHaveLength(0);
  const data = { ...node.data, interfaceUi: { exposedParameters: ['samples'], exposedPorts: ['samples'], exposedBulkSlots: ['samples'] } };
  act(() => root.render(<OrpheusNode id={node.id} data={data} />));
  expect(container.querySelector('[data-handle="samples"]')).not.toBeNull();
  expect(container.querySelector('[data-handle="ctl:samples"]')).toBeNull();
  expect(container.querySelectorAll('.ctl-row')).toHaveLength(1);
  expect(container.querySelectorAll('.bulk-interface-row')).toHaveLength(1);
  expect(canEditParameter(parameters[1])).toBe(true);
});

test('实际参数连线和公开音频端口强制显露，公开参数不锁定显示', () => {
  const decorated = { ...node, data: { ...node.data, exportedAudioPorts: { out: { id: 'export' } },
    exportedControlParameters: { ready: { id: 'ready' } } } };
  decorated.data.interfaces = interfaceState(decorated, [
    { source: 'upstream', target: 'host', sourceHandle: 'out', targetHandle: 'samples' },
    { source: 'host', target: 'target', sourceHandle: 'ctl:level', targetHandle: 'ctl:gain', type: 'control' },
  ]);
  act(() => root.render(<><OrpheusNode id="host" data={decorated.data} />
    <ParamPanel node={decorated} viewKey="main" /></>));
  expect(container.querySelector('[data-handle="samples"]')).not.toBeNull();
  expect(container.querySelector('[data-handle="out"]')).not.toBeNull();
  expect(container.querySelector('[data-handle="ctl:level"]')).not.toBeNull();
  expect(container.querySelector('[aria-label="显露 exposedPorts:samples"]').disabled).toBe(true);
  expect(container.querySelector('[aria-label="显露 exposedParameters:level"]').disabled).toBe(true);
  expect(container.querySelector('[aria-label="显露 exposedParameters:ready"]').disabled).toBe(false);
  expect(container.querySelector('[aria-label="显露 exposedParameters:ready"]').checked).toBe(false);
  expect(container.textContent).toContain('上游供数，禁止宿主写入');
});

test('公开参数默认不铺到画布，保留映射及完整审查', () => {
  const data = { ...node.data, exportedControlParameters: {
    samples: { id: 'public_samples', direction: 'input' }, legacy: { id: 'public_legacy', direction: 'input' },
  } };
  const state = interfaceState({ id: node.id, data });
  expect(state.exposedParameters).toEqual([]);
  expect(state.lockedParameters).toEqual([]);
  act(() => root.render(<><OrpheusNode id={node.id} data={data} /><ParamPanel node={{ ...node, data }} viewKey="main" /></>));
  expect(container.querySelectorAll('.node-controls')).toHaveLength(0);
  expect(container.querySelectorAll('[data-interface^="parameter:"]')).toHaveLength(parameters.length);
  expect(container.querySelector('[data-interface="parameter:samples"]').textContent).toContain('外层公开：public_samples');
  expect(container.querySelector('[aria-label="显露 exposedParameters:samples"]').disabled).toBe(false);
  expect(data.exportedControlParameters.samples.id).toBe('public_samples');
});

test('只读定义的显露属于工程布局，公开参数仍可选择显示', () => {
  const onInterfaceChange = jest.fn();
  const data = { ...node.data, exportedControlParameters: { legacy: { id: 'public_legacy', direction: 'input' } } };
  act(() => root.render(<ParamPanel node={{ ...node, data }} viewKey="sub:readonly" todosReadOnly
    onInterfaceChange={onInterfaceChange} />));
  const toggle = container.querySelector('[aria-label="显露 exposedParameters:legacy"]');
  expect(toggle.disabled).toBe(false);
  act(() => toggle.click());
  expect(onInterfaceChange).toHaveBeenCalledWith('exposedParameters', 'legacy', true);
  expect(container.querySelector('[aria-label="显露 exposedBulkSlots:samples"]').disabled).toBe(false);
});

test('初始化配置独立审查，显露后不出现运行期方块或橙色引线', () => {
  const schemas = [
    { id: 'select', name: '选择映射', type: 'string', update_policy: 'restart_required', bindable: true, control_source: true },
    { id: 'channels', type: 'int', affects_signature: true },
    { id: 'override', type: 'string', update_policy: 'block_boundary' },
    { id: 'level', type: 'float', access: 'read', control_source: true },
  ];
  const data = { component: 'test.router', parameters: schemas, params: { select: '1,2' },
    interfaceUi: { exposedParameters: ['select', 'channels', 'override', 'level'] },
    exportedControlParameters: { select: { id: 'base_select', direction: 'input' } } };
  act(() => root.render(<><OrpheusNode id="router" data={data} />
    <ParamPanel node={{ id: 'router', data }} viewKey="main" /></>));
  expect(schemas.map(isInitializationParameter)).toEqual([true, true, false, false]);
  expect(container.querySelectorAll('.initial-config-row')).toHaveLength(2);
  expect(container.querySelectorAll('.ctl-row')).toHaveLength(2);
  expect(container.querySelector('.export-config-pin')).not.toBeNull();
  expect(container.querySelector('.export-control-pin')).toBeNull();
  expect(container.querySelector('[data-handle="ctl:select"]')).toBeNull();
  expect(container.querySelector('[data-handle="ctl:channels"]')).toBeNull();
  expect(container.querySelector('[data-handle="ctl:override"]')).toBeNull();
  expect(container.querySelector('[data-handle="ctl:level"]')).not.toBeNull();
  const groups = [...container.querySelectorAll('.interface-section > summary')].map((element) => element.textContent);
  expect(groups).toContain('初始化配置（2）');
  expect(groups).toContain('运行期参数（1）');
  expect(groups).toContain('观测 / 交付状态（1）');
  expect(container.querySelector('[data-interface="parameter:select"]').textContent).toContain('启动前注入');
});

test('显露 Buffer 独立于参数显示，不因多参数重复绘制', () => {
  const data = { ...node.data, interfaceUi: { exposedBulkSlots: ['samples'] } };
  act(() => root.render(<OrpheusNode id={node.id} data={data} />));
  expect(container.querySelectorAll('.bulk-interface-row')).toHaveLength(1);
  expect(container.querySelectorAll('.ctl-row')).toHaveLength(0);
  act(() => root.render(<OrpheusNode id={node.id} data={{ ...data, interfaceUi: {
    ...data.interfaceUi, exposedParameters: ['samples', 'legacy'],
  } }} />));
  expect(container.querySelectorAll('.bulk-interface-row')).toHaveLength(1);
  expect(container.querySelectorAll('.ctl-row')).toHaveLength(2);
});

test('宿主条件和交付观测准确，不伪造 ready/valid 或触发', () => {
  expect(bindingStatus(ports[0], node.data)).toContain('无上游时可由宿主交付');
  expect(bindingStatus(ports[0], node.data)).toContain('未观测交付状态');
  expect(bindingStatus(ports[0], node.data)).toContain('写入不触发 Task');
  expect(bindingStatus(ports[0], { ...node.data, params: { mode: 1 } })).toContain('不生效');
  expect(bindingStatus(ports[0], { ...node.data, probe: { ready: true } })).toContain('ready=true');
  const outputBinding = { ...ports[0], id: 'delivery', direction: 'output' };
  expect(bindingStatus(outputBinding, { ...node.data, interfaces: { upstreamPorts: ['samples'] } }))
    .toContain('上游供数，禁止宿主写入');
  expect(portSignature({ channels: 2 }, node.data)).toContain('32帧');
});

test('旧控制连线保留禁连接锚点，不授予普通配置 bindable', () => {
  const data = { ...node.data };
  data.interfaces = interfaceState({ id: 'host', data }, [
    { source: 'source', target: 'host', sourceHandle: 'ctl:level', targetHandle: 'ctl:samples', type: 'control' },
  ]);
  act(() => root.render(<OrpheusNode id="host" data={data} />));
  expect(container.querySelector('[data-handle="ctl:samples"]').dataset.connectable).toBe('false');
  expect(data.parameters[0].bindable).toBeUndefined();
});

test('保存重载布局显露，不混入算法参数；复制粘贴保留节点 UI', async () => {
  const ui = { exposedParameters: ['ready'], exposedPorts: ['out'], exposedBulkSlots: ['samples'] };
  const doc = { graph: { nodes: [{ id: 'host', component: 'test.host', params: { mode: 0 }, position: { x: 10, y: 20 } }], connections: [] },
    layout: { viewport: { zoom: 1 }, node_ui: { main: { host: ui } } } };
  const { views } = docToViews(doc, catalog);
  expect(views.main.nodes[0].data.interfaceUi).toEqual(ui);
  expect(views.main.nodes[0].data.bulkSlots).toHaveLength(1);
  const saved = viewsToDoc(views, [], doc);
  expect(saved.graph.nodes[0].params).toEqual({ mode: 0 });
  expect(saved.layout.viewport).toEqual({ zoom: 1 });
  expect(saved.layout.node_ui.main.host).toEqual(ui);
  expect((await docToViewsAsync(saved, catalog)).views.main.nodes[0].data.interfaceUi).toEqual(ui);
  const pasted = pasteFlowSelection(views.main, copyFlowSelection(views.main, ['host']));
  expect(pasted.nodes[1].data.interfaceUi).toEqual(ui);
});

test('普通公开配置 input 不自动提升 bindable，显式能力保留', () => {
  const sub = subCatalogEntry({ id: 'test', public_parameters: [
    { id: 'config', direction: 'input' }, { id: 'control', direction: 'input', bindable: true },
  ] });
  expect(sub.parameters[0].bindable).toBeUndefined();
  expect(sub.parameters[1].bindable).toBe(true);
});

test('普通可写配置可导出，不要求 bindable，不包含只读输入', () => {
  const onAddParameter = jest.fn();
  act(() => root.render(<SubPortsPanel sub={{ id: 'sub', name: '子图', ports: [] }}
    viewNodes={[node]} catalogById={{ 'test.host': catalog[0] }} onAddParameter={onAddParameter} />));
  const select = container.querySelectorAll('select')[3];
  expect(select.querySelector('[value="host:legacy"]')).not.toBeNull();
  expect(select.querySelector('[value="host:valid"]')).toBeNull();
  act(() => {
    select.value = 'host:legacy';
    select.dispatchEvent(new Event('change', { bubbles: true }));
  });
  act(() => [...container.querySelectorAll('button')].at(-1).click());
  expect(onAddParameter).toHaveBeenCalledWith('input', 'host:legacy', parameters[4]);
  expect(onAddParameter.mock.calls[0][2].bindable).toBeUndefined();
});

test.each(['setting', 'probe', undefined])('全局参数页宿主 ready 保留读写声明，但不直接编辑交付状态（kind=%s）', (kind) => {
  const onNodeParamChange = jest.fn();
  const component = { ...catalog[0], parameters: [
    { ...parameters[1], kind }, { ...parameters[2], kind: 'probe' },
    { ...parameters[3], kind: 'probe' },
  ] };
  act(() => root.render(<ParamBrowser projectName="mock" views={{ main: { nodes: [node], edges: [] } }}
    catalogById={{ 'test.host': component }} rt={{ running: false, probes: {} }} presets={[]}
    onNodeParamChange={onNodeParamChange} />));
  const readyField = container.querySelector('.pb-delivery-status');
  expect(readyField.textContent).toContain('ready');
  expect(readyField.textContent).toContain('可读写');
  expect(readyField.textContent).toContain('此面板不直接写入');
  expect(readyField.querySelector('input')).toBeNull();
  expect(onNodeParamChange).not.toHaveBeenCalled();
  for (const id of ['valid', 'missing_blocks']) {
    const row = [...container.querySelectorAll('.pb-probe')].find((element) => element.textContent.includes(id));
    expect(row.textContent).toContain('只读');
    expect(row.querySelector('input')).toBeNull();
  }
});

test('普通 readback 可读写配置仍能编辑，只有声明的宿主握手被隔离', () => {
  const component = { ...catalog[0], ports: [], bulk_slots: [], parameters: [parameters[1]] };
  const onNodeParamChange = jest.fn();
  act(() => root.render(<ParamBrowser projectName="mock" views={{ main: { nodes: [node], edges: [] } }}
    catalogById={{ 'test.host': component }} rt={{ running: false, probes: {} }} presets={[]}
    onNodeParamChange={onNodeParamChange} />));
  const checkbox = container.querySelector('.param-field input[type="checkbox"]');
  expect(checkbox.disabled).toBe(false);
  act(() => checkbox.click());
  expect(onNodeParamChange).toHaveBeenCalledWith('main', 'host', 'ready', true, 'host');
});

test('实际 ready=true 仅作为观测显示，旧设计值保存时归一为初始化 false', () => {
  const data = { ...node.data, params: { mode: 0, ready: true }, probe: { ready: true } };
  const onParamChange = jest.fn();
  act(() => root.render(<ParamPanel node={{ ...node, data }} viewKey="main" onParamChange={onParamChange} />));
  const checkbox = container.querySelector('[data-interface="parameter:ready"] .param-field input');
  expect(checkbox.checked).toBe(true);
  expect(checkbox.disabled).toBe(true);
  expect(container.querySelector('[data-interface="parameter:ready"]').textContent).toContain('宿主交付状态：true');
  act(() => checkbox.click());
  expect(onParamChange).not.toHaveBeenCalled();
  expect(canEditParameter(parameters[1])).toBe(true);
  expect(canEditDesignParameter(parameters[1], data)).toBe(false);
  const views = { main: { nodes: [{ ...node, position: { x: 10, y: 20 }, data }], edges: [] } };
  const saved = viewsToDoc(views, [], {});
  expect(saved.graph.nodes[0].params.ready).toBe(false);
  expect(data.params.ready).toBe(true);
  expect(docToViews(saved, catalog).views.main.nodes[0].data.params.ready).toBe(false);
});

test('布局引用输入经显露保存转为独立内嵌布局，不混合 file/node_ui', () => {
  const document = { graph: { nodes: [
    { id: 'host', component: 'test.host', params: { mode: 0 } },
  ], connections: [] }, layout: { file: 'custom.layout.yaml' } };
  const { views } = docToViews(document, catalog);
  expect(views.main.nodes[0].data.interfaceUi).toBeUndefined();
  views.main.nodes[0].data.interfaceUi = { exposedPorts: ['samples'] };
  const saved = viewsToDoc(views, [], document);
  expect(saved.layout).toEqual({
    version: 1, views: {}, node_ui: { main: { host: { exposedPorts: ['samples'] } } },
  });
  expect(saved.layout).not.toHaveProperty('file');
  expect(document.layout).toEqual({ file: 'custom.layout.yaml' });
  expect(docToViews(saved, catalog).views.main.nodes[0].data.interfaceUi.exposedPorts).toEqual(['samples']);
});

test('全局参数页即使实时会话运行且数量正确，也不能提交宿主绑定 PCM 槽', () => {
  const component = { ...catalog[0], bulk_slots: [{ ...catalog[0].bulk_slots[0], count: 16 }] };
  const onWriteBulk = jest.fn();
  act(() => root.render(<ParamBrowser projectName="mock" views={{ main: { nodes: [node], edges: [] } }}
    catalogById={{ 'test.host': component }} rt={{ running: true, probes: {} }} presets={[]}
    onWriteBulk={onWriteBulk} />));
  const write = container.querySelector('.pb-write');
  const input = write.closest('.pb-bulk').querySelector('input[type="text"]');
  const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
  act(() => {
    setter.call(input, Array(16).fill(0).join(', '));
    input.dispatchEvent(new Event('input', { bubbles: true }));
  });
  expect(write.disabled).toBe(true);
  expect(write.closest('.pb-bulk').textContent).toContain('此面板不提交音频');
  act(() => write.click());
  expect(onWriteBulk).not.toHaveBeenCalled();
});
