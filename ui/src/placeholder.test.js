import { collectViewTodos, newTodo, placeholderPorts, PLACEHOLDER_ID } from './placeholder';
import { docToViews, viewsToDoc, resolvePorts } from './graphUtils';

const catalog = [{ id: PLACEHOLDER_ID, package_type: 'source', ports: [], parameters: [] }];

test('占位端口支持各端口独立名称与通道数，拒绝伪类型和重复 id', () => {
  const ports = [{ id: 'mic', direction: 'input', channels: 4 }, { id: 'out', direction: 'output', channels: 1 }];
  expect(resolvePorts(catalog[0], { interface: JSON.stringify(ports) }).map((p) => p.channels)).toEqual([4, 1]);
  expect(() => placeholderPorts(JSON.stringify([ports[0], ports[0]]))).toThrow();
  expect(() => placeholderPorts(JSON.stringify([{ ...ports[0], type: 'event' }]))).toThrow();
  expect(() => placeholderPorts('bad JSON')).toThrow();
});

test('TODO 往返包含未实例化子图，已完成不能掩盖占位模块', () => {
  const todo = { ...newTodo(), source_ref: 'model.c:filter', status: 'done' };
  const doc = {
    version: '0.1.0', graph: { nodes: [{ id: 'p', component: PLACEHOLDER_ID, todos: [todo] }], connections: [] },
    subcomponents: [{
      id: 'unused', ports: [], graph: {
        nodes: [{ id: 'leaf', component: PLACEHOLDER_ID, todos: [{ ...todo, status: 'blocked' }] }],
        connections: [],
      },
    }],
  };
  const { views, subsMeta } = docToViews(doc, catalog);
  const items = collectViewTodos(views);
  expect(items).toHaveLength(2);
  expect(items[0].synthetic).toBe(true);
  expect(items[1].location).toBe('sub:unused/leaf#todo-1');
  expect(collectViewTodos(views, true)).toHaveLength(3);
  const saved = viewsToDoc(views, subsMeta, doc);
  expect(saved.graph.nodes[0].todos).toEqual([todo]);
  expect(saved.subcomponents[0].graph.nodes[0].todos[0].source_ref).toBe('model.c:filter');
});

test('TODO id 可在删除和添加后保持唯一', () => {
  expect(newTodo([{ id: 'todo-1' }, { id: 'todo-3' }]).id).toBe('todo-2');
});
