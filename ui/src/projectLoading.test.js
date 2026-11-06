import { docToViews, docToViewsAsync } from './graphUtils';

const document = {
  graph: { nodes: [{ id: 'root', component: 'test.leaf', position: { x: 3, y: 4 } }], connections: [] },
  subcomponents: Array.from({ length: 5 }, (_, index) => ({
    id: `test.sub${index}`, ports: [],
    graph: { nodes: [{ id: 'child', component: 'test.leaf' }], connections: [] },
  })),
};
const catalog = [{ id: 'test.leaf', name: '基础节点', ports: [], parameters: [] }];
const originalFrame = window.requestAnimationFrame;

beforeAll(() => {
  window.requestAnimationFrame = (callback) => setTimeout(() => callback(performance.now()), 0);
});
afterAll(() => {
  window.requestAnimationFrame = originalFrame;
});

test('分批转换与原同步转换逐字段一致，报告真实视图和节点计数', async () => {
  const updates = [];
  const result = await docToViewsAsync(document, catalog, {
    onProgress: (progress) => updates.push(progress),
  });
  expect(result).toEqual(docToViews(document, catalog));
  expect(updates.map((item) => item.completed)).toEqual([0, 1, 2, 3, 4, 5, 6]);
  expect(updates[0]).toEqual({ completed: 0, total: 6, nodeCount: 0 });
  expect(updates.at(-1)).toEqual({ completed: 6, total: 6, nodeCount: 6 });
});

test('无节点的工程仍转换主视图并正常完成', async () => {
  const empty = { graph: { nodes: [], connections: [] } };
  await expect(docToViewsAsync(empty, [])).resolves.toEqual(docToViews(empty, []));
});

test('已中止的载入不转换任何视图', async () => {
  const controller = new AbortController();
  controller.abort();
  const onProgress = jest.fn();
  await expect(docToViewsAsync(document, catalog, {
    signal: controller.signal, onProgress,
  })).rejects.toMatchObject({ name: 'AbortError' });
  expect(onProgress).not.toHaveBeenCalled();
});

test('转换途中中止时不继续生成后续视图或返回成功结果', async () => {
  const controller = new AbortController();
  const updates = [];
  await expect(docToViewsAsync(document, catalog, {
    signal: controller.signal,
    onProgress: (progress) => {
      updates.push(progress.completed);
      if (progress.completed === 1) controller.abort();
    },
  })).rejects.toMatchObject({ name: 'AbortError' });
  expect(updates).toEqual([0, 1]);
});
