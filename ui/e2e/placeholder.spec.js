const { test, expect } = require('@playwright/test');

const name = `e2e_placeholder_${Date.now()}`;

test.beforeAll(async ({ request }) => {
  expect((await request.post('/api/projects', { data: { name, from_example: 'placeholder_demo' } })).ok()).toBeTruthy();
  const doc = await (await request.get(`/api/projects/${name}`)).json();
  doc.subcomponents = [{
    id: 'unused', name: '未实例化模块', ports: [],
    graph: {
      nodes: [{ id: 'leaf', component: 'orpheus.builtin.placeholder', todos: [{
        id: 'leaf', title: '未实例化模块也须实现', status: 'pending', next_step: '读取 LeafProcess',
      }], position: { x: 100, y: 100 } }],
      connections: [],
    },
  }];
  expect((await request.put(`/api/projects/${name}`, { data: doc })).ok()).toBeTruthy();
});

test.afterAll(async ({ request }) => {
  await request.delete(`/api/projects/${name}`);
});

test('人工配置占位接口、编辑 TODO、全量查找并保存恢复', async ({ page, request }) => {
  await page.goto('/');
  await page.locator('.toolbar select').first().selectOption(name);
  await expect(page.getByText(`已打开工程 ${name}`)).toBeVisible();
  await page.getByRole('button', { name: 'TODO', exact: true }).click();
  await expect(page.getByRole('button', { name: 'main/pending#implement', exact: true })).toBeVisible();
  await expect(page.getByRole('button', { name: 'sub:unused/leaf#leaf', exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'main/pending#implement', exact: true }).click();
  const panel = page.locator('.rightbar');
  await panel.getByLabel('下一步（从哪里继续、做什么）').fill('读取 model.c 的 FrontProcess，提取 FIR');
  await panel.getByLabel('端口 2 通道数').fill('3');
  await panel.getByRole('button', { name: '＋ 输出', exact: true }).click();
  await panel.locator('.param-field').filter({ hasText: '输出路由（' }).locator('input').fill('0,-1,-1');
  await expect(page.locator('.react-flow__node[data-id="pending"]').getByText('out0', { exact: true })).toBeVisible();
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect(page.getByText(new RegExp(`已保存 ${name}`))).toBeVisible();
  const doc = await (await request.get(`/api/projects/${name}`)).json();
  const pending = doc.graph.nodes.find((node) => node.id === 'pending');
  expect(pending.todos[0].next_step).toContain('FrontProcess');
  expect(JSON.parse(pending.params.interface).find((port) => port.id === 'monitor').channels).toBe(3);
  expect(JSON.parse(pending.params.interface)).toHaveLength(4);
  const compiled = await request.post(`/api/projects/${name}/compile`);
  expect(compiled.ok()).toBeTruthy();
  expect((await compiled.json()).warnings).toHaveLength(1);
  const report = await (await request.get(`/api/projects/${name}/todos`)).json();
  expect(report.items).toHaveLength(2);
  await page.reload();
  await page.locator('.toolbar select').first().selectOption(name);
  await page.getByRole('button', { name: 'TODO', exact: true }).click();
  await expect(page.getByText('下一步：读取 model.c 的 FrontProcess，提取 FIR')).toBeVisible();
  await page.getByRole('button', { name: 'sub:unused/leaf#leaf', exact: true }).click();
  await expect(page.locator('.tab.active').filter({ hasText: '未实例化模块' })).toBeVisible();
  await expect(panel.getByLabel('下一步（从哪里继续、做什么）')).toHaveValue('读取 LeafProcess');
  await page.getByRole('button', { name: '组件库', exact: true }).click();
  await page.getByPlaceholder('搜索组件（中/英文模糊）').fill('orpheus.builtin.placeholder');
  await page.locator('.palette-item').filter({ hasText: 'orpheus.builtin.placeholder' }).dragTo(page.locator('.canvas'));
  await expect(panel.getByRole('textbox', { name: '任务', exact: true })).toHaveValue('实现并验证此模块');
  await expect(panel.locator('.placeholder-port-row')).toHaveCount(2);
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect(page.getByText(new RegExp(`已保存 ${name}`))).toBeVisible();
  const updated = await (await request.get(`/api/projects/${name}`)).json();
  const added = updated.subcomponents.find((sub) => sub.id === 'unused').graph.nodes.find((node) => node.id !== 'leaf');
  expect(added.component).toBe('orpheus.builtin.placeholder');
  expect(added.todos[0].status).toBe('pending');
});
