import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import TodoPanel, { NodeTodos } from './TodoPanel';

const views = {
  main: { nodes: [
    { id: 'live', data: { component: 'test.leaf', todos: [
      { id: 'open', title: '真实待办', status: 'pending' },
      { id: 'done', title: '历史完成', status: 'done' },
    ] } },
  ] },
  'sub:unused': { nodes: [
    { id: 'old', data: { component: 'test.leaf', todos: [
      { id: 'old-open', title: '未使用定义待办', status: 'pending' },
    ] } },
  ] },
};

let container;
let root;
beforeEach(() => {
  global.IS_REACT_ACT_ENVIRONMENT = true;
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
});
afterEach(() => {
  act(() => root.unmount());
  container.remove();
});

test('默认只显示活动未完成项，筛选保留真实记录与定位行为', () => {
  const onLocate = jest.fn();
  act(() => root.render(<TodoPanel views={views} onLocate={onLocate} />));
  expect(container.querySelector('.todo-panel-list')).not.toBeNull();
  expect(container.querySelectorAll('.todo-item')).toHaveLength(1);
  expect(container.textContent).toContain('未完成 1 / 当前显示 1');
  expect(container.textContent).toContain('另有 1 条未使用子图记录未显示');
  act(() => container.querySelector('.todo-item button').click());
  expect(onLocate).toHaveBeenCalledWith('main', 'live');
  const toggles = container.querySelectorAll('input[type="checkbox"]');
  act(() => toggles[1].click());
  expect(container.querySelectorAll('.todo-item')).toHaveLength(2);
  act(() => toggles[0].click());
  expect(container.querySelectorAll('.todo-item')).toHaveLength(3);
  expect(views.main.nodes[0].data.todos[1].status).toBe('done');
});

test('空清单保留可伸缩容器与独立列表', () => {
  act(() => root.render(<TodoPanel views={{ main: { nodes: [] } }} onLocate={jest.fn()} />));
  expect(container.querySelector('[aria-label="工程 TODO"]')).not.toBeNull();
  expect(container.querySelector('.todo-panel-header')).not.toBeNull();
  expect(container.querySelector('.todo-panel-list').textContent).toContain('当前范围无 TODO');
});

test.each([
  ['空清单', [], false, false],
  ['有待办', views.main.nodes[0].data.todos, false, false],
  ['占位节点', views.main.nodes[0].data.todos, true, false],
  ['只读节点', views.main.nodes[0].data.todos, true, true],
])('节点 TODO 默认折叠：%s', (_, todos, isPlaceholder, readOnly) => {
  act(() => root.render(<NodeTodos todos={todos} isPlaceholder={isPlaceholder} readOnly={readOnly} />));
  expect(container.querySelector('details').open).toBe(false);
  expect(container.querySelector('summary').textContent)
    .toBe(`TODO（${todos.filter((todo) => todo.status !== 'done').length} 未完成）`);
});

test('手动展开后仍可添加待办，数据更新不重新折叠', () => {
  const onChange = jest.fn();
  act(() => root.render(<NodeTodos onChange={onChange} />));
  const section = container.querySelector('details');
  act(() => { section.open = true; });
  act(() => container.querySelector('button').click());
  const next = onChange.mock.calls[0][0];
  expect(next).toHaveLength(1);
  act(() => root.render(<NodeTodos todos={next} onChange={onChange} />));
  expect(section.open).toBe(true);
  expect(container.querySelector('summary').textContent).toBe('TODO（1 未完成）');
  expect(container.querySelectorAll('.todo-item')).toHaveLength(1);
});
