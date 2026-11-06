import React from 'react';
import { createRoot } from 'react-dom/client';
import { act } from 'react';
import ProjectSettings from './ProjectSettings';
import OrpheusNode from './OrpheusNode';
import DriveEdge from './DriveEdge';
import SignalLegend from './SignalLegend';
import ParamPanel from './ParamPanel';
import { NodeActionsContext } from './NodeActionsContext';

jest.mock('./nodeWidgets', () => ({ NODE_WIDGETS: {} }));
jest.mock('reactflow', () => ({
  Handle: ({ id, type, position, className, style, isConnectable }) => <span data-handle={id}
    data-direction={type} data-position={position} data-connectable={String(isConnectable)}
    className={className} style={style} />,
  Position: { Left: 'left', Right: 'right', Top: 'top' },
  NodeResizer: () => null,
  useUpdateNodeInternals: () => jest.fn(),
  BaseEdge: () => null,
  EdgeLabelRenderer: ({ children }) => <foreignObject>{children}</foreignObject>,
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
  act(() => root.render(<svg><DriveEdge id="drive" data={{ every: 2 }} /></svg>));
  expect(container.textContent).toContain('每2次·相位1');
  act(() => root.render(<svg><DriveEdge id="drive" data={{ every: 3, phase: 0 }} /></svg>));
  expect(container.textContent).toContain('每3次·相位0');
  act(() => root.render(<svg><DriveEdge id="drive" /></svg>));
  expect(container.textContent).toBe('驱动 →');
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
  expect(container.querySelector('.legend-drive').textContent).toBe('驱动（节点级箭头）');
  expect(container.querySelector('.legend-control')).toBeNull();
  act(() => root.render(<SignalLegend flowMode="task_sequence" showControlLinks />));
  expect(container.textContent).toBe('音频控制驱动（节点级箭头）');
  act(() => root.render(<SignalLegend flowMode="dataflow" showControlLinks />));
  expect(container.textContent).toBe('音频控制');
});

test('纯驱动源是无PCM端口的触发标记，不渲染普通算法节点', () => {
  act(() => root.render(
    <NodeActionsContext.Provider value={{ showControlLinks: false }}>
      <OrpheusNode id="trigger" data={{ label: '100Hz触发', component: 'orpheus.builtin.task_source',
        flowMode: 'task_sequence', clockSource: true, params: { frequency_hz: 100 }, ports: [] }} />
    </NodeActionsContext.Provider>
  ));
  expect(container.querySelector('.drive-source-marker').textContent).toContain('纯触发 · 100 Hz');
  expect(container.querySelector('.orpheus-node')).toBeNull();
  expect(container.querySelectorAll('[data-handle]')).toHaveLength(1);
  expect(container.querySelector('[data-handle="drv:out"]')).not.toBeNull();
});

test('驱动路由不可见，组件本体接收入口，音频端口及Buffer展示独立', () => {
  const render = (source) => act(() => root.render(
    <NodeActionsContext.Provider value={{ showControlLinks: false }}>
      <OrpheusNode id="audio" data={{ label: '音频源', component: source
        ? 'orpheus.builtin.buffer_source' : 'orpheus.builtin.gain',
        flowMode: 'task_sequence', clockSource: source, blockSize: 32, params: { channels: 2 },
        ports: [{ id: 'out', type: 'audio', direction: 'output', channels: 'param:channels' }] }} />
    </NodeActionsContext.Provider>
  ));
  render(true);
  expect(container.querySelector('.source-buffer-summary').textContent).toContain('2ch × 32帧');
  expect(container.querySelector('[data-handle="out"]').dataset.direction).toBe('source');
  expect(container.querySelector('[data-handle="drv:out"]').dataset.position).toBe('right');
  expect(container.querySelector('[data-handle="drv:out"]').style.opacity).toBe('0');
  render(false);
  expect(container.querySelector('[data-handle="drv:in"]').dataset.position).toBe('top');
  expect(container.querySelector('.node-drive')).toBeNull();
  expect(container.querySelector('.node-drive-anchors')).toBeNull();
  expect(container.querySelector('.drive-entry-anchor')).toBeNull();
});

test('圆点标记归属，只有驱动连线模式允许连接到组件本体', () => {
  const render = (driveLinking) => act(() => root.render(
    <NodeActionsContext.Provider value={{ showControlLinks: false, driveLinking }}>
      <OrpheusNode id="gain" data={{ component: 'orpheus.builtin.gain', flowMode: 'task_sequence',
        driveMemberships: [{ id: 'clock:1:0', color: 'red', label: '100Hz入口' }] }} />
    </NodeActionsContext.Provider>
  ));
  render(false);
  expect(container.querySelector('.drive-domain-dot').title).toBe('100Hz入口');
  expect(container.querySelector('.drive-domain-dot').style.backgroundColor).toBe('red');
  expect(container.querySelector('[data-handle="drv:in"]').style.pointerEvents).toBe('none');
  render(true);
  expect(container.querySelector('[data-handle="drv:in"]').dataset.connectable).toBe('true');
  expect(container.querySelector('[data-handle="drv:out"]').dataset.connectable).toBe('false');
});

test('驱动箭头编辑分频，拒绝非法相位并提交有效配置', () => {
  const onTimingChange = jest.fn();
  act(() => root.render(<svg><DriveEdge id="drive" data={{ every: 2, phase: 1, onTimingChange }} /></svg>));
  act(() => container.querySelector('.drive-timing-button').click());
  const input = container.querySelector('[aria-label="驱动相位"]');
  const setValue = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
  act(() => {
    setValue.call(input, '2');
    input.dispatchEvent(new Event('input', { bubbles: true }));
  });
  act(() => container.querySelector('form').dispatchEvent(new Event('submit', { bubbles: true, cancelable: true })));
  expect(onTimingChange).not.toHaveBeenCalled();
  expect(container.querySelector('[role="alert"]')).not.toBeNull();
  act(() => {
    setValue.call(input, '0');
    input.dispatchEvent(new Event('input', { bubbles: true }));
  });
  act(() => container.querySelector('form').dispatchEvent(new Event('submit', { bubbles: true, cancelable: true })));
  expect(onTimingChange).toHaveBeenCalledWith({ every: 2, phase: 0 });
  expect(container.querySelector('form')).toBeNull();
});

test('显式驱动参数面板不再要求所属Task，纯驱动频率仍可编辑', () => {
  act(() => root.render(<ParamPanel flowMode="task_sequence" viewKey="main" node={{
    id: 'trigger', data: { component: 'orpheus.builtin.task_source', label: '纯驱动源',
      clockSource: true, params: { frequency_hz: 100 }, parameters: [{
        id: 'frequency_hz', name: '驱动频率', type: 'int', default: 100,
        readback: true, persistent: true,
      }],
    },
  }} onParamChange={jest.fn()} />));
  expect(container.textContent).not.toContain('所属 Task');
  expect(container.textContent).toContain('驱动源：宿主推进入口');
  expect(container.querySelector('input[type="number"]').value).toBe('100');
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
