const { test, expect } = require('./editor-test');

const name = `e2e_source_${Date.now()}`;

test.beforeAll(async ({ request }) => {
  expect((await request.post('/api/projects', { data: { name } })).ok()).toBeTruthy();
});

test.afterAll(async ({ request }) => {
  await request.delete(`/api/projects/${name}`);
});

test('落盘失败诊断、修订生成、私有注册和拖入画布可保存恢复', async ({ page, request }) => {
  test.setTimeout(60000);
  // 不枚举声卡、不构建宿主；本用例只验证编辑与独立组件脚本链路。
  await page.route('**/api/devices', (route) => route.fulfill({ json: { capture: [], playback: [] } }));
  await page.route('**/api/projects', (route) => route.request().method() === 'GET'
    ? route.fulfill({ json: [{ name }] }) : route.continue());
  const errors = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto('/');
  await expect(page.getByText(`已打开工程 ${name}`, { exact: true })).toBeVisible();
  await page.getByRole('button', { name: '＋ 新建源码组件', exact: true }).click();
  const dialog = page.getByRole('dialog', { name: '新建源码组件' });
  const configuration = dialog.getByLabel('组件配置');
  const initial = await configuration.inputValue();
  await configuration.fill('interfaces: [');
  await dialog.getByRole('button', { name: '保存并生成', exact: true }).click();
  await expect(dialog.getByRole('status')).toContainText('配置已保存；校验失败');
  const saved = await (await request.get(`/api/projects/${name}/source-components/my_effect`)).json();
  expect(saved.configuration).toBe('interfaces: [');
  expect((await (await request.get(`/api/projects/${name}`)).json()).imports).toBeUndefined();
  await configuration.fill(initial.replace('project.custom.effect', 'project.custom.e2e_effect'));
  await dialog.getByRole('button', { name: '保存并生成', exact: true }).click();
  await expect(dialog.getByRole('status')).toContainText('生成并注册成功');
  await dialog.getByRole('button', { name: '关闭', exact: true }).click();
  await page.getByPlaceholder('搜索组件（中/英文模糊）').fill('project.custom.e2e_effect');
  const item = page.locator('.palette-item').filter({ hasText: 'project.custom.e2e_effect' });
  await expect(item).toBeVisible();
  await item.dragTo(page.locator('.canvas'));
  await expect(page.locator('.react-flow__node')).toHaveCount(1);
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect(page.getByText(new RegExp(`已保存 ${name}`))).toBeVisible();
  const document = await (await request.get(`/api/projects/${name}`)).json();
  expect(document.imports).toEqual(['components/my_effect/component.yaml']);
  expect(document.graph.nodes[0].component).toBe('project.custom.e2e_effect');
  await page.reload();
  await expect(page.locator('.react-flow__node')).toHaveCount(1);
  await page.getByRole('button', { name: '＋ 新建源码组件', exact: true }).click();
  await dialog.getByLabel('包名').fill('trigger_effect');
  await dialog.getByRole('button', { name: '填入纯触发示例', exact: true }).click();
  await expect(configuration).toHaveValue(/type: trigger/);
  await dialog.getByRole('button', { name: '保存并生成', exact: true }).click();
  await expect(dialog.getByRole('status')).toContainText('生成并注册成功');
  await dialog.getByRole('button', { name: '关闭', exact: true }).click();
  await page.getByPlaceholder('搜索组件（中/英文模糊）').fill('project.custom.trigger');
  await page.locator('.palette-item').filter({ hasText: 'project.custom.trigger' })
    .dragTo(page.locator('.canvas'), { targetPosition: { x: 500, y: 300 } });
  await expect(page.locator('.react-flow__node')).toHaveCount(2);
  await page.getByLabel('显露 exposedPorts:next', { exact: true }).check();
  const triggerId = await page.locator('.react-flow__node.selected').getAttribute('data-id');
  const trigger = page.locator(`.react-flow__node[data-id="${triggerId}"]`);
  await expect(trigger).toContainText('触发（无 PCM）');
  await expect(trigger).toContainText('驱动 1');
  await expect(trigger).not.toContainText('ch');
  const audio = page.locator(`.react-flow__node[data-id="${document.graph.nodes[0].id}"]`);
  await audio.click();
  await page.getByLabel('显露 exposedPorts:input', { exact: true }).check();
  await trigger.locator('[data-handleid="next"]')
    .dragTo(audio.locator('[data-handleid="input"]'));
  await expect(page.getByText('接口连接被拒绝：', { exact: false })).toBeVisible();
  await expect.poll(async () =>
    (await (await request.get(`/api/projects/${name}`)).json()).graph.nodes.length).toBe(2);
  const withTrigger = await (await request.get(`/api/projects/${name}`)).json();
  expect(withTrigger.graph.connections).toEqual([]);
  expect(withTrigger.imports).toContain('components/trigger_effect/component.yaml');
  await page.locator('.palette-item').filter({ hasText: 'project.custom.trigger' })
    .dragTo(page.locator('.canvas'), { targetPosition: { x: 450, y: 120 } });
  await expect(page.locator('.react-flow__node')).toHaveCount(3);
  const secondId = await page.locator('.react-flow__node.selected').getAttribute('data-id');
  const second = page.locator(`.react-flow__node[data-id="${secondId}"]`);
  await page.getByLabel('显露 exposedPorts:tick', { exact: true }).check();
  await trigger.locator('[data-handleid="next"]').dragTo(second.locator('[data-handleid="tick"]'));
  await expect(page.locator('.react-flow__edge')).toHaveCount(1);
  await expect.poll(async () =>
    (await (await request.get(`/api/projects/${name}`)).json()).graph.connections.length).toBe(1);
  const linked = await (await request.get(`/api/projects/${name}`)).json();
  expect(linked.graph.connections[0]).toMatchObject({ from: `${triggerId}:next`, to: `${secondId}:tick` });
  expect(linked.drive_connections || []).toEqual([]);
  expect(errors).toEqual([]);
});
