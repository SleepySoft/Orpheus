import React, { useState } from 'react';
import { createRoot } from 'react-dom/client';
import { act } from 'react';
import ParameterWorkspace from './ParameterWorkspace';
import ParamPanel from './ParamPanel';

let container;
let root;
let frame;
let popup;
beforeEach(() => {
  global.IS_REACT_ACT_ENVIRONMENT = true;
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
  frame = document.createElement('iframe');
  document.body.appendChild(frame);
  popup = frame.contentWindow;
  popup.focus = jest.fn();
  popup.close = jest.fn();
  jest.spyOn(window, 'open').mockReturnValue(popup);
});
afterEach(() => {
  act(() => root.unmount());
  container.remove();
  frame.remove();
  jest.restoreAllMocks();
});

function Harness() {
  const [open, setOpen] = useState(true);
  const [value, setValue] = useState('原值');
  return (
    <>
      <button onClick={() => setOpen(true)}>展开侧栏</button>
      <ParameterWorkspace open={open} onOpenChange={setOpen}>
        <input aria-label="测试配置" value={value} onChange={(event) => setValue(event.target.value)} />
        <button onClick={() => setValue('新值')}>修改配置</button>
      </ParameterWorkspace>
    </>
  );
}

const click = (target) => act(() => target.dispatchEvent(new MouseEvent('click', { bubbles: true })));
const button = (scope, text) => [...scope.querySelectorAll('button')].find((item) => item.textContent === text);

test('收起释放侧栏，重新展开保留配置', () => {
  act(() => root.render(<Harness />));
  click(button(container, '修改配置'));
  click(button(container, '收起 »'));
  expect(container.querySelector('.parameter-workspace').hidden).toBe(true);
  click(button(container, '展开侧栏'));
  expect(container.querySelector('.parameter-workspace').hidden).toBe(false);
  expect(container.querySelector('input').value).toBe('新值');
});

test('独立窗口复制样式、同步编辑、收回侧栏并关闭窗口', () => {
  const style = document.createElement('style');
  style.textContent = '.test-popup-style { color: red; }';
  document.head.appendChild(style);
  act(() => root.render(<Harness />));
  click(button(container, '独立窗口'));
  style.remove();
  expect(window.open).toHaveBeenCalledWith('', '_blank', expect.stringContaining('resizable=yes'));
  expect(popup.document.title).toBe('Orpheus · 参数面板');
  expect(popup.document.head.textContent).toContain('.test-popup-style');
  expect(container.querySelector('.rightbar')).toBeNull();
  click(button(popup.document, '修改配置'));
  expect(popup.document.querySelector('input').value).toBe('新值');
  expect(popup.close).not.toHaveBeenCalled();
  click(button(container, '展开侧栏'));
  click(button(container, '聚焦窗口'));
  expect(popup.focus).toHaveBeenCalledTimes(2);
  click(button(popup.document, '收回侧栏'));
  expect(container.querySelector('input').value).toBe('新值');
  expect(popup.close).toHaveBeenCalledTimes(1);
  expect(popup.document.querySelector('.rightbar')).toBeNull();
});

test('关闭独立窗口自动恢复侧栏，主页面离开时关闭子窗口', () => {
  act(() => root.render(<Harness />));
  click(button(container, '独立窗口'));
  act(() => popup.dispatchEvent(new Event('beforeunload')));
  expect(container.querySelector('.parameter-workspace').hidden).toBe(false);
  click(button(container, '独立窗口'));
  act(() => window.dispatchEvent(new Event('pagehide')));
  expect(popup.close).toHaveBeenCalledTimes(2);
});

test('浏览器拦截时明确提示，配置面板仍可用', () => {
  window.open.mockReturnValue(null);
  act(() => root.render(<Harness />));
  click(button(container, '独立窗口'));
  expect(container.querySelector('[role="alert"]').textContent).toContain('请允许本站弹出窗口');
  expect(container.querySelector('.parameter-workspace').hidden).toBe(false);
  click(button(container, '修改配置'));
  expect(container.querySelector('input').value).toBe('新值');
});

test('卸载面板关闭所属窗口并移除主页面离开监听', () => {
  act(() => root.render(<Harness />));
  click(button(container, '独立窗口'));
  act(() => root.render(null));
  expect(popup.close).toHaveBeenCalledTimes(1);
  act(() => window.dispatchEvent(new Event('pagehide')));
  expect(popup.close).toHaveBeenCalledTimes(1);
});

test('参数、Buffer 与端口按分组折叠，观测更新不重新展开', () => {
  const node = { id: 'test', data: {
    component: 'test.component', parameters: [{ id: 'gain', type: 'float', default: 1 }],
    bulkSlots: [{ id: 'samples', count: 16 }], ports: [{ id: 'out', direction: 'output' }],
  } };
  act(() => root.render(<ParamPanel node={node} viewKey="main" />));
  const sections = container.querySelectorAll('.interface-section');
  expect(sections).toHaveLength(3);
  act(() => { sections[0].open = false; });
  act(() => root.render(<ParamPanel node={{ ...node, data: { ...node.data, probe: { level: 2 } } }}
    viewKey="main" />));
  expect(sections[0].open).toBe(false);
  expect(sections[1].open).toBe(true);
  expect(sections[2].open).toBe(true);
});
