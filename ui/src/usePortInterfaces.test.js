import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import usePortInterfaces, { interfaceSnapshot, resolvedNodeInterfaces } from './usePortInterfaces';
import { resolveProjectInterfaces } from './api';
import { docToViews, viewsToDoc } from './graphUtils';

jest.mock('./api', () => ({ resolveProjectInterfaces: jest.fn() }));

let root;
let container;
let latest;
const documentSnapshot = {
  block_size: 32, layout: { node_ui: {} },
  graph: { nodes: [{ id: 'n', component: 'test', params: { width: 3 }, position: { x: 1, y: 2 } }],
    connections: [] },
};
function Preview({ document = documentSnapshot, project = 'demo', view = 'main' }) {
  latest = usePortInterfaces(project, document, view);
  return null;
}
async function advance() {
  await act(async () => { jest.advanceTimersByTime(180); });
}
beforeEach(() => {
  global.IS_REACT_ACT_ENVIRONMENT = true;
  jest.useFakeTimers();
  resolveProjectInterfaces.mockReset();
  latest = null;
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
});
afterEach(() => {
  act(() => root.unmount());
  container.remove();
  jest.useRealTimers();
});

test('拖动、显露和观测不重发，结构/参数变化才请求；请求不带布局', async () => {
  resolveProjectInterfaces.mockResolvedValue({ nodes: {} });
  act(() => root.render(<Preview />));
  await advance();
  expect(resolveProjectInterfaces).toHaveBeenCalledTimes(1);
  const moved = { ...documentSnapshot, layout: { node_ui: { main: { n: { exposedPorts: ['in'] } } } },
    graph: { ...documentSnapshot.graph, nodes: documentSnapshot.graph.nodes.map((node) => ({
      ...node, position: { x: 200, y: 200 }, todos: [{ text: '待办' }],
    })) } };
  act(() => root.render(<Preview document={moved} />));
  await advance();
  expect(resolveProjectInterfaces).toHaveBeenCalledTimes(1);
  expect(resolveProjectInterfaces.mock.calls[0][1].layout).toBeUndefined();
  expect(resolveProjectInterfaces.mock.calls[0][1].graph.nodes[0].position).toBeUndefined();
  const changed = { ...moved, block_size: 64 };
  act(() => root.render(<Preview document={changed} />));
  expect(latest).toBeNull();
  await advance();
  expect(resolveProjectInterfaces).toHaveBeenCalledTimes(2);
});

test('快速改值合并请求，切换视图取消请求并忽略迟到响应', async () => {
  let finishOld;
  resolveProjectInterfaces.mockImplementationOnce(() => new Promise((resolve) => { finishOld = resolve; }))
    .mockResolvedValueOnce({ nodes: { fresh: { status: 'resolved', ports: [] } } });
  act(() => root.render(<Preview />));
  act(() => root.render(<Preview document={{ ...documentSnapshot, block_size: 64 }} />));
  await advance();
  expect(resolveProjectInterfaces).toHaveBeenCalledTimes(1);
  const oldSignal = resolveProjectInterfaces.mock.calls[0][3].signal;
  act(() => root.render(<Preview view="sub:inner" />));
  expect(oldSignal.aborted).toBe(true);
  expect(latest).toBeNull();
  await advance();
  expect(latest.nodes.fresh.status).toBe('resolved');
  await act(async () => finishOld({ nodes: { stale: {} } }));
  expect(latest.nodes.stale).toBeUndefined();
});

test('请求错误明确显示，不能用前端 Task 块长或表达式猜签名', async () => {
  resolveProjectInterfaces.mockRejectedValue({ response: { data: { detail: '引用循环' } } });
  act(() => root.render(<Preview />));
  await advance();
  const raw = { ports: [{ id: 'in', channels: 'param:width*2' }], blockSize: 32 };
  const result = resolvedNodeInterfaces(raw, latest, 'n');
  expect(result.portResolution).toEqual({ status: 'error', message: '引用循环' });
  expect(result.ports).toBe(raw.ports);
  expect(resolvedNodeInterfaces(raw, null, 'n').portResolution.status).toBe('pending');
});

test('后端逐端口结果覆盖模板且来源不持久化进工程', () => {
  const derived = { id: 'in', direction: 'input', channels: 6, frames: 96, sample_rate: 24000,
    sample_format: 'f32', resolution: { status: 'resolved', endpoint: 'outer__inner:in' } };
  const result = resolvedNodeInterfaces({}, { nodes: { n: { status: 'resolved', ports: [derived] } } }, 'n');
  expect(result.ports).toEqual([derived]);
  const { views, subsMeta } = docToViews(documentSnapshot, [{ id: 'test', ports: [] }]);
  views.main.nodes[0].data = { ...views.main.nodes[0].data, ...result };
  const saved = viewsToDoc(views, subsMeta, documentSnapshot);
  expect(JSON.stringify(saved)).not.toContain('outer__inner');
  expect(JSON.stringify(saved)).not.toContain('portResolution');
  expect(interfaceSnapshot(saved).graph.nodes[0].params).toEqual({ width: 3 });
});
