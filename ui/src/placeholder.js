export const PLACEHOLDER_ID = 'orpheus.builtin.placeholder';
export const DEFAULT_INTERFACE = '[{"id":"in","direction":"input","channels":2},{"id":"out","direction":"output","channels":2}]';

export function placeholderPorts(value = DEFAULT_INTERFACE) {
  let ports;
  try {
    ports = JSON.parse(value);
  } catch {
    throw new Error('占位接口必须是 JSON 端口数组');
  }
  if (!Array.isArray(ports) || ports.length < 1 || ports.length > 64) {
    throw new Error('占位模块须声明 1 至 64 个端口');
  }
  const seen = new Set();
  const counts = { input: 0, output: 0 };
  return ports.map((port) => {
    if (!port || typeof port !== 'object'
      || Object.keys(port).some((key) => !['id', 'direction', 'channels'].includes(key))
      || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(port.id || '')
      || seen.has(port.id)
      || !['input', 'output'].includes(port.direction)
      || !Number.isInteger(port.channels) || port.channels < 1 || port.channels > 64) {
      throw new Error('占位端口须有唯一合法名称、input/output 方向和 1 至 64 通道');
    }
    seen.add(port.id);
    counts[port.direction] += 1;
    if (counts[port.direction] > 32) throw new Error('每个方向最多 32 个端口');
    return { ...port, type: 'audio', sample_format: 'f32' };
  });
}

export function newTodo(todos = []) {
  let index = 1;
  while (todos.some((todo) => todo.id === `todo-${index}`)) index += 1;
  return {
    id: `todo-${index}`, title: '实现并验证此模块', status: 'pending',
    source_ref: '', intent: '', next_step: '', acceptance: '', blocker: '',
  };
}

export function collectViewTodos(views, includeDone = false) {
  const items = [];
  for (const [view, graph] of Object.entries(views)) {
    for (const node of graph.nodes || []) {
      const records = [...(node.data.todos || [])];
      const isPlaceholder = node.data.component === PLACEHOLDER_ID || node.data.packageType === 'placeholder';
      if (isPlaceholder && !records.some((todo) => todo.status !== 'done')) {
        records.push({
          id: '__placeholder__', title: '替换占位模块并验证算法；补充源码索引与下一步', status: 'pending', synthetic: true,
        });
      }
      if (node.data.packageType === 'placeholder') {
        for (const blocker of node.data.blockers || []) {
          records.push({
            id: `__blocker__${blocker.id}`, title: blocker.reason, status: 'blocked',
            source_ref: blocker.source_ref || '', synthetic: true,
          });
        }
      }
      for (const todo of records) {
        if (includeDone || todo.status !== 'done') {
          items.push({ ...todo, view, node: node.id, component: node.data.component,
            location: `${view}/${node.id}#${todo.id}` });
        }
      }
    }
  }
  return items;
}
