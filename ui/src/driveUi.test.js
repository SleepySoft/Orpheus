import React from 'react';
import { createRoot } from 'react-dom/client';
import { act } from 'react';
import ProjectSettings from './ProjectSettings';
import OrpheusNode from './OrpheusNode';
import DriveEdge from './DriveEdge';
import SignalLegend from './SignalLegend';
import { NodeActionsContext } from './NodeActionsContext';

jest.mock('./nodeWidgets', () => ({ NODE_WIDGETS: {} }));
jest.mock('reactflow', () => ({
  Handle: ({ id, type }) => <span data-handle={id} data-direction={type} />,
  Position: { Left: 'left', Right: 'right' },
  NodeResizer: () => null,
  useUpdateNodeInternals: () => jest.fn(),
  BaseEdge: () => null,
  EdgeLabelRenderer: ({ children }) => <>{children}</>,
  getBezierPath: () => ['', 0, 0],
}));

let container;
let root;
beforeEach(() => {
  global.IS_REACT_ACT_ENVIRONMENT = true;
  container = window.document.createElement('div');
  window.document.body.appendChild(container);
  root = createRoot(container);
});
afterEach(() => {
  act(() => root.unmount());
  container.remove();
});

test('执行编排选择器默认兼容模式，主动启用后保存 task_sequence', () => {
  const onSave = jest.fn();
  act(() => root.render(<ProjectSettings doc={{}} onSave={onSave} onClose={() => {}} />));
  const select = container.querySelector('#project-flow-mode');
  expect(select.value).toBe('dataflow');
  act(() => {
    select.value = 'task_sequence';
    select.dispatchEvent(new Event('change', { bubbles: true }));
  });
  const save = Array.from(container.querySelectorAll('button')).find((button) => button.textContent.includes('保存'));
  act(() => save.click());
  expect(onSave).toHaveBeenCalledWith(expect.objectContaining({ flow_mode: 'task_sequence' }));
});

test('旧工程保存设置不强制写入默认模式', () => {
  const onSave = jest.fn();
  act(() => root.render(<ProjectSettings doc={{}} onSave={onSave} onClose={() => {}} />));
  const save = Array.from(container.querySelectorAll('button')).find((button) => button.textContent.includes('保存'));
  act(() => save.click());
  expect(onSave.mock.calls[0][0]).not.toHaveProperty('flow_mode');
});

test('虚拟执行 Handle 随模式切换挂载；时钟源不出现执行输入', () => {
  const render = (flowMode, clockSource = false) => act(() => root.render(
    <NodeActionsContext.Provider value={{ showControlLinks: false }}>
      <OrpheusNode id="test" data={{ component: 'test.atomic', flowMode, clockSource,
        ports: [{ id: 'in', direction: 'input' }, { id: 'out', direction: 'output' }] }} />
    </NodeActionsContext.Provider>
  ));
  render('dataflow');
  expect(container.querySelectorAll('[data-handle^="drv:"]')).toHaveLength(0);
  render('task_sequence');
  expect(container.querySelector('[data-handle="drv:in"]').dataset.direction).toBe('target');
  expect(container.querySelector('[data-handle="drv:out"]').dataset.direction).toBe('source');
  render('task_sequence', true);
  expect(container.querySelector('[data-handle="drv:in"]')).toBeNull();
  expect(container.querySelector('[data-handle="drv:out"]')).not.toBeNull();
  render('dataflow');
  expect(container.querySelectorAll('[data-handle^="drv:"]')).toHaveLength(0);
  expect(container.querySelectorAll('[data-handle]')).toHaveLength(2);
});

test('执行边显式展示非默认节拍与默认相位，默认边保持简洁', () => {
  act(() => root.render(<DriveEdge id="drive" data={{ every: 2 }} />));
  expect(container.textContent).toContain('每2次·相位1');
  act(() => root.render(<DriveEdge id="drive" data={{ every: 3, phase: 0 }} />));
  expect(container.textContent).toContain('每3次·相位0');
  act(() => root.render(<DriveEdge id="drive" />));
  expect(container.textContent).toBe('执行 →');
});

test('复合组件渲染可调用的执行输入和输出', () => {
  act(() => root.render(
    <NodeActionsContext.Provider value={{ showControlLinks: false }}>
      <OrpheusNode id="chain" data={{ component: 'sub:test.chain', flowMode: 'task_sequence' }} />
    </NodeActionsContext.Provider>
  ));
  expect(container.querySelector('[data-handle="drv:in"]')).not.toBeNull();
  expect(container.querySelector('[data-handle="drv:out"]')).not.toBeNull();
});

test('执行图例仅在任务驱动模式显示，不依赖参数控制开关', () => {
  act(() => root.render(<SignalLegend flowMode="dataflow" showControlLinks={false} />));
  expect(container.textContent).toBe('音频');
  act(() => root.render(<SignalLegend flowMode="task_sequence" showControlLinks={false} />));
  expect(container.querySelector('.legend-drive').textContent).toBe('执行');
  expect(container.querySelector('.legend-control')).toBeNull();
  act(() => root.render(<SignalLegend flowMode="task_sequence" showControlLinks />));
  expect(container.textContent).toBe('音频控制执行');
  act(() => root.render(<SignalLegend flowMode="dataflow" showControlLinks />));
  expect(container.textContent).toBe('音频控制');
});

test('SourceTask 提示随编排模式切换显示，兼容 Task 模板仍保留', () => {
  act(() => root.render(<ProjectSettings doc={{}} onSave={() => {}} onClose={() => {}} />));
  expect(container.textContent).not.toContain('每个源（Source）生成独立的逻辑 Task');
  const select = container.querySelector('#project-flow-mode');
  act(() => {
    select.value = 'task_sequence';
    select.dispatchEvent(new Event('change', { bubbles: true }));
  });
  expect(container.textContent).toContain('每个源（Source）生成独立的逻辑 Task');
  expect(container.textContent).toContain('frequency_hz 覆盖模板块量子');
  expect(container.textContent).toContain('实际 Task 频率以编译后的 plan 为准');
  expect(container.querySelector('[aria-label="default 块长度"]')).not.toBeNull();
});
