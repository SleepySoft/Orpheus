import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import Palette from './Palette';

const components = [
  { id: 'test.shared', name: '测试通用', category: '基础/测试' },
  { id: 'test.local', name: '测试定制', category: '基础/测试', project_local: true, user_owned: true },
  { id: 'test.owned', name: '测试私有', category: '音效/测试', user_owned: true },
  { id: 'test.imported', name: '重复定义', category: '工程/模型' },
  { id: 'test.hidden', name: '测试隐藏', ui_hidden: true, project_local: true },
];
const subsMeta = [
  { id: 'test.imported', name: '测试导入子组件', read_only: true },
  { id: 'test.editable', name: '测试自建子组件' },
];
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

const ids = () => Array.from(container.querySelectorAll('.palette-item-id'), (node) => node.textContent);
const section = (name) => container.querySelector(`button[aria-label="${name}"]`);
const render = (props = {}) => act(() => root.render(
  <Palette components={components} subsMeta={subsMeta} {...props} />
));

test('工程分组置顶且默认收起，展开去重并保留只读和全局管理规则', () => {
  const onDeleteSub = jest.fn();
  const onPromoteComponent = jest.fn();
  render({ onDeleteSub, onPromoteComponent });
  const headers = container.querySelectorAll('.palette > .palette-category > .palette-category-header');
  expect(headers[0]).toBe(section('工程子组件'));
  expect(headers[1]).toBe(section('工程定制组件'));
  expect(section('工程子组件').getAttribute('aria-expanded')).toBe('false');
  expect(ids()).toEqual(['test.shared']);
  act(() => section('工程子组件').click());
  act(() => section('工程定制组件').click());
  expect(ids()).toEqual(['sub:test.imported', 'sub:test.editable', 'test.local', 'test.owned', 'test.shared']);
  const imported = container.querySelector('.palette-item');
  expect(imported.querySelector('.palette-item-del')).toBeNull();
  const editable = container.querySelectorAll('.palette-item')[1];
  act(() => editable.querySelector('.palette-item-del').click());
  expect(onDeleteSub).toHaveBeenCalledWith('sub:test.editable');
  const local = container.querySelectorAll('.palette-item')[2];
  expect(local.querySelector('.palette-manage')).toBeNull();
  const owned = container.querySelectorAll('.palette-item')[3];
  act(() => owned.querySelector('.palette-manage button').click());
  expect(onPromoteComponent).toHaveBeenCalledWith('test.owned');
  act(() => section('工程子组件').click());
  expect(ids()).not.toContain('sub:test.imported');
});

test('搜索包含收起的工程分组，过滤重复和隐藏项，拖拽ID不变', () => {
  render();
  const input = container.querySelector('input');
  act(() => {
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(input, '测试');
    input.dispatchEvent(new Event('input', { bubbles: true }));
  });
  expect(ids()).toEqual(['sub:test.imported', 'sub:test.editable', 'test.local', 'test.owned', 'test.shared']);
  for (const [index, id] of [[0, 'sub:test.imported'], [2, 'test.local']]) {
    const transfer = { setData: jest.fn(), effectAllowed: '' };
    const event = new Event('dragstart', { bubbles: true });
    Object.defineProperty(event, 'dataTransfer', { value: transfer });
    act(() => container.querySelectorAll('.palette-item')[index].dispatchEvent(event));
    expect(transfer.setData).toHaveBeenCalledWith('application/orpheus-component', id);
    expect(transfer.effectAllowed).toBe('move');
  }
});

test('只有子组件时不误报后端无组件', () => {
  render({ components: [] });
  expect(section('工程子组件')).not.toBeNull();
  expect(container.textContent).not.toContain('后端未连接');
});
