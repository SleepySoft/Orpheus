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
    nodes: [
      {
        id: 'src', component: 'orpheus.builtin.signal_gen', task: 'producer',
        params: { frequency: 440, amplitude: 0.2, channels: 1 }, position: { x: 20, y: 80 },
      },
      {
        id: 'meter', component: 'orpheus.builtin.level_detect', task: 'producer',
        params: { channels: 1 }, position: { x: 220, y: 80 },
      },
      {
        id: 'chain1', component: 'sub:chain', task: 'producer',
        params: { gain: -6 }, position: { x: 430, y: 80 },
      },
      {
        id: 'sink', component: 'orpheus.builtin.null_sink', task: 'producer',
        params: { channels: 1 }, position: { x: 650, y: 80 },
      },
      {
        id: 'gain_node', component: 'orpheus.builtin.gain', task: 'producer',
        params: { gain_db: 0, channels: 1 }, position: { x: 260, y: 300 },
      },
    ],
    connections: [
      { from: 'src:out', to: 'meter:in' },
      { from: 'meter:out', to: 'chain1:in' },
      { from: 'chain1:out', to: 'sink:in' },
    ],
  },
  control_connections: [{ from: 'meter:level', to: 'chain1:gain' }],
  subcomponents: [{
    id: 'chain', name: 'Gain Chain',
    ports: [
      { id: 'in', direction: 'input', maps_to: 'gain:in' },
      { id: 'out', direction: 'output', maps_to: 'gain:out' },
    ],
    public_parameters: [{
      id: 'gain', name: '增益', direction: 'input', maps_to: 'gain:gain_db',
      type: 'float', default: -6, update_policy: 'smoothed',
    }],
    graph: {
      nodes: [{
        id: 'gain', component: 'orpheus.builtin.gain', task: 'producer',
        params: { gain_db: -6, channels: 1 }, position: { x: 160, y: 120 },
      }],
      connections: [],
    },
  }],
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

test('配置 Task、区分链路并定位导出引脚', async ({ page, request }, testInfo) => {
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
  expect(saved.control_connections).toEqual([{ from: 'meter:level', to: 'chain1:gain' }]);

  await page.getByRole('button', { name: '教学', exact: true }).click();
  const lesson = page.locator('.lesson-panel');
  await expect(lesson.getByText('多任务检查')).toBeVisible();
  await lesson.getByRole('button', { name: '检查当前工程' }).click();
  await expect(lesson.getByText('1/1 项通过')).toBeVisible();
  await lesson.getByRole('button', { name: '关闭' }).click();

  const controlToggle = page.locator('label.autosave').filter({ hasText: '控制链路' }).locator('input');
  await expect(controlToggle).not.toBeChecked();
  await expect(page.locator('.control-edge-path')).toHaveCount(0);
  await page.screenshot({ path: testInfo.outputPath('control-links-off.png'), fullPage: true });
  await controlToggle.check();
  await expect(page.locator('.node-controls')).toHaveCount(3);
  await expect(page.locator('.legend-control')).toBeVisible();
  await page.screenshot({ path: testInfo.outputPath('control-links-on.png'), fullPage: true });
  await expect(page.locator('.control-edge-path')).toHaveCount(1);

  const exportedInput = page.locator('.react-flow__node').filter({ hasText: 'chain1' })
    .locator('.export-input .export-handle');
  const exportedOutput = page.locator('.react-flow__node').filter({ hasText: 'chain1' })
    .locator('.export-output .export-handle');
  await expect(exportedInput).toBeVisible();
  await expect(exportedOutput).toBeVisible();
  const chainBox = await page.locator('.react-flow__node').filter({ hasText: 'chain1' })
    .locator('.orpheus-node').boundingBox();
  const inputBox = await exportedInput.boundingBox();
  const outputBox = await exportedOutput.boundingBox();
  expect(chainBox.x - (inputBox.x + inputBox.width / 2)).toBeGreaterThan(28);
  expect(outputBox.x + outputBox.width / 2 - (chainBox.x + chainBox.width)).toBeGreaterThan(28);
  await exportedInput.click();
  await expect(page.locator('.subports')).toBeVisible();
  await expect(page.locator('.subport-row.export-highlight').filter({ hasText: 'in' })).toBeVisible();
  await page.screenshot({ path: testInfo.outputPath('audio-export-revealed.png'), fullPage: true });

  await page.locator('.tab').filter({ hasText: '主图' }).click();
  await page.locator('.react-flow__node').filter({ hasText: 'chain1' })
    .locator('.export-control-input .export-control-handle').click();
  await expect(page.locator('.control-export-row.export-highlight').filter({ hasText: 'gain' })).toBeVisible();
});
