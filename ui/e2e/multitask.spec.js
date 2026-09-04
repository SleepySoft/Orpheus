const { test, expect } = require('@playwright/test');

const projectName = `e2e_multitask_${Date.now()}`;

const project = {
  version: '0.1.0',
  metadata: { name: projectName },
  sample_rate: 48000,
  block_size: 128,
  buffer_size: 0,
  double_bank: 'auto',
  target: 'auto',
  tasks: [
    { id: 'producer', name: 'Producer', sample_rate: 48000, block_size: 24, priority: 1 },
    { id: 'consumer', name: 'Consumer', sample_rate: 48000, block_size: 32, priority: 0 },
  ],
  lesson: {
    title: '多任务检查',
    steps: [{ title: '确认节点', body: '检查 gain_node 组件。' }],
    checks: [{
      id: 'gain', label: 'Gain 节点存在', type: 'node_component',
      node: 'gain_node', component: 'orpheus.builtin.gain',
    }],
  },
  graph: {
    nodes: [{
      id: 'gain_node', component: 'orpheus.builtin.gain', task: 'producer',
      params: { gain_db: 0, channels: 1 }, position: { x: 200, y: 160 },
    }],
    connections: [],
  },
};

test.beforeAll(async ({ request }) => {
  const created = await request.post('/api/projects', { data: { name: projectName } });
  expect(created.ok()).toBeTruthy();
  const saved = await request.put(`/api/projects/${projectName}`, { data: project });
  expect(saved.ok()).toBeTruthy();
});

test.afterAll(async ({ request }) => {
  await request.delete(`/api/projects/${projectName}`);
});

test('配置 Task 并保存节点归属', async ({ page, request }) => {
  await page.goto('/');
  await page.locator('.toolbar select').first().selectOption(projectName);
  await expect(page.getByText(`已打开工程 ${projectName}`)).toBeVisible();

  await page.getByRole('button', { name: '⚙ 设置' }).click();
  const modal = page.locator('.modal');
  await expect(modal.getByText('任务 (Task)')).toBeVisible();
  await modal.getByRole('button', { name: '新增 Task' }).click();
  await modal.getByRole('button', { name: '保存' }).click();

  await page.locator('.react-flow__node').filter({ hasText: 'gain_node' }).click();
  await page.locator('.param-field').filter({ hasText: '所属 Task' }).locator('select').selectOption('task_3');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect(page.getByText(new RegExp(`已保存 ${projectName}`))).toBeVisible();

  const response = await request.get(`/api/projects/${projectName}`);
  expect(response.ok()).toBeTruthy();
  const saved = await response.json();
  expect(saved.tasks.map((task) => task.id)).toEqual(['producer', 'consumer', 'task_3']);
  expect(saved.graph.nodes.find((node) => node.id === 'gain_node').task).toBe('task_3');

  await page.getByRole('button', { name: '教学', exact: true }).click();
  const lesson = page.locator('.lesson-panel');
  await expect(lesson.getByText('多任务检查')).toBeVisible();
  await lesson.getByRole('button', { name: '检查当前工程' }).click();
  await expect(lesson.getByText('1/1 项通过')).toBeVisible();
});
