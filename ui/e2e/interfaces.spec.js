const { test, expect } = require('@playwright/test');

test.use({ viewport: { width: 1600, height: 1000 } });

const component = {
  id: 'test.interface', name: '统一接口测试',
  ports: [
    { id: 'samples', type: 'audio', direction: 'input', format: 'f32', channels: 2, block_size: 8,
      host_binding: { slot: 'samples', access: 'write', status: 'ready', when: { mode: 0 } } },
    { id: 'out', type: 'audio', direction: 'output', format: 's16', channels: 6, block_size: 'param:frames' },
  ],
  parameters: [
    { id: 'mode', type: 'int', default: 0, access: 'read_write' },
    { id: 'frames', type: 'int', default: 16, access: 'write' },
    { id: 'samples', name: '普通配置', type: 'float', default: 2, access: 'write' },
    { id: 'ready', type: 'bool', default: false, readback: true, access: 'read_write' },
    { id: 'valid', type: 'bool', readback: true, access: 'read' },
    { id: 'missing_blocks', type: 'int', access: 'read' },
    { id: 'legacy', type: 'float', default: 3, readback: true },
  ],
  bulk_slots: [{ id: 'samples', type: 'float', access: 'read_write', count: 16 }],
};
const initialDocument = {
  version: '1.0.0', metadata: { name: 'interfaces-preview' }, block_size: 32,
  layout: { file: 'project.layout.yaml' },
  graph: {
    nodes: [
      { id: 'host', component: component.id, params: { mode: 0, frames: 16, samples: 2, ready: true },
        position: { x: 100, y: 100 } },
      { id: 'connected', component: component.id, params: { mode: 0, frames: 16 },
        position: { x: 600, y: 100 } },
    ],
    connections: [{ from: 'host:out', to: 'connected:samples' }],
  },
};

test('完整参数/Buffer/签名审查、独立显露保存重载及上游宿主写保护', async ({ page }) => {
  let saved;
  let document = initialDocument;
  const writes = [];
  const errors = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.context().route('**/api/**', async (route) => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    if (request.method() === 'PUT' && path === '/api/projects/interfaces-preview') {
      saved = request.postDataJSON();
      if (saved.layout?.file && saved.layout?.node_ui) {
        await route.fulfill({ status: 400, json: { detail: '布局引用不能混合内嵌字段' } });
        return;
      }
      document = saved;
      await route.fulfill({ json: saved });
      return;
    }
    if (request.method() !== 'GET') {
      writes.push(path);
      await route.abort();
      return;
    }
    let data;
    if (path === '/api/projects') data = [{ name: 'interfaces-preview' }];
    else if (path === '/api/components' || path.endsWith('/components')) data = [component];
    else if (path === '/api/examples') data = [];
    else if (path === '/api/devices') data = { capture: [], playback: [] };
    else if (path.endsWith('/node_notes')) data = { notes: {} };
    else if (path.endsWith('/rt/status')) data = { running: false, logs: [], probes: {} };
    else if (path === '/api/projects/interfaces-preview') data = document;
    else { await route.fulfill({ status: 404, json: { detail: `未模拟：${path}` } }); return; }
    await route.fulfill({ json: data });
  });
  await page.goto('/');
  await expect(page.getByRole('dialog', { name: '正在载入工程' })).toHaveCount(0);
  await page.getByRole('checkbox', { name: '自动保存', exact: true }).uncheck();
  const host = page.locator('.react-flow__node[data-id="host"]');
  const connected = page.locator('.react-flow__node[data-id="connected"]');
  await expect(host.locator('[data-handleid="samples"]')).toHaveCount(0);
  await expect(host.locator('[data-handleid="out"]')).toBeVisible();
  await expect(connected.locator('[data-handleid="samples"]')).toBeVisible();
  await host.locator('.node-title').click();
  const panel = page.locator('.sidebar');
  await expect(panel.locator('[data-interface^="parameter:"]')).toHaveCount(7);
  await expect(panel.locator('[data-interface^="bulk:"]')).toHaveCount(1);
  await expect(panel.locator('[data-interface^="port:"]')).toHaveCount(2);
  await expect(panel.locator('[data-interface="parameter:valid"] .param-field input')).toBeDisabled();
  await expect(panel.locator('[data-interface="parameter:missing_blocks"] .param-field input')).toBeDisabled();
  await expect(panel.locator('[data-interface="parameter:ready"] .param-field input')).toBeDisabled();
  await expect(panel.locator('[data-interface="parameter:ready"]')).toContainText('可读写');
  await expect(panel.locator('[data-interface="parameter:ready"]')).toContainText('由完整 PCM 提交原子更新');
  await expect(panel.locator('[data-interface="parameter:legacy"]')).toContainText('权限未显式声明');
  await expect(panel.locator('[data-interface="port:samples"]')).toContainText('f32 · 2ch · 8帧');
  await expect(panel.locator('[data-interface="port:out"]')).toContainText('s16 · 6ch · 16帧');
  await expect(panel.locator('[data-interface="port:samples"]')).toContainText('写入不触发 Task');
  await expect(panel.locator('[data-interface="port:samples"]')).toContainText('未观测交付状态');
  const parameterGroup = panel.locator('.interface-section').filter({ has: page.getByText('运行期参数（4）', { exact: true }) });
  await parameterGroup.locator('summary').click();
  await expect(panel.locator('[data-interface="parameter:samples"]')).not.toBeVisible();
  await parameterGroup.locator('summary').click();
  await page.getByRole('button', { name: '收起 »', exact: true }).click();
  await expect(panel).not.toBeVisible();
  await page.getByTitle('展开参数面板', { exact: true }).click();
  await expect(panel).toBeVisible();
  const popupPromise = page.waitForEvent('popup');
  await page.getByRole('button', { name: '独立窗口', exact: true }).click();
  const popup = await popupPromise;
  popup.on('pageerror', (error) => errors.push(error.message));
  await expect(popup).toHaveTitle('Orpheus · 参数面板');
  const popupPanel = popup.locator('.sidebar');
  await expect(popupPanel.locator('[data-interface^="parameter:"]')).toHaveCount(7);
  await expect(page.locator('.rightbar')).toHaveCount(0);
  await expect.poll(() => popupPanel.locator('.interface-section-grid').first()
    .evaluate((element) => getComputedStyle(element).gridTemplateColumns.split(' ').length))
    .toBeGreaterThan(1);
  await popupPanel.locator('[data-interface="parameter:samples"] .param-field input').fill('5.5');
  await connected.locator('.node-title').click();
  await expect(popupPanel.locator('[data-interface="port:samples"]')).toContainText('上游供数，禁止宿主写入');
  await host.locator('.node-title').click();
  await expect(popupPanel.locator('[data-interface="parameter:samples"] .param-field input')).toHaveValue('5.5');
  await popup.getByRole('checkbox', { name: '显露 exposedPorts:samples', exact: true }).check();
  await expect(host.locator('[data-handleid="samples"]')).toBeVisible();
  await popup.close({ runBeforeUnload: true });
  await expect(panel).toBeVisible();
  await expect(panel.locator('[data-interface="parameter:samples"] .param-field input')).toHaveValue('5.5');
  await page.getByRole('checkbox', { name: '显露 exposedPorts:samples', exact: true }).uncheck();
  const dockPromise = page.waitForEvent('popup');
  await page.getByRole('button', { name: '独立窗口', exact: true }).click();
  const dockPopup = await dockPromise;
  dockPopup.on('pageerror', (error) => errors.push(error.message));
  await dockPopup.getByRole('button', { name: '收回侧栏', exact: true }).click();
  await expect.poll(() => dockPopup.isClosed()).toBe(true);
  await expect(panel).toBeVisible();
  await page.getByRole('checkbox', { name: '显露 exposedPorts:samples', exact: true }).check();
  await page.getByRole('checkbox', { name: '显露 exposedParameters:samples', exact: true }).check();
  await page.getByRole('checkbox', { name: '显露 exposedBulkSlots:samples', exact: true }).check();
  await expect(host.locator('[data-handleid="samples"]')).toBeVisible();
  await expect(host.locator('[data-handleid="ctl:samples"]')).toHaveCount(0);
  await expect(host.locator('.bulk-interface-row')).toHaveCount(1);
  await panel.locator('[data-interface="parameter:samples"] .param-field input').fill('4.5');

  await connected.locator('.node-title').click();
  await expect(panel.locator('[data-interface="port:samples"]')).toContainText('上游供数，禁止宿主写入');
  await expect(page.getByRole('checkbox', { name: '显露 exposedPorts:samples', exact: true })).toBeDisabled();
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect.poll(() => saved).toBeTruthy();
  expect(saved.layout.node_ui.main.host).toEqual({
    exposedPorts: ['samples'], exposedParameters: ['samples'], exposedBulkSlots: ['samples'],
  });
  expect(saved.layout).toMatchObject({ version: 1, views: {} });
  expect(saved.layout).not.toHaveProperty('file');
  expect(saved.graph.nodes.find((node) => node.id === 'host').params.samples).toBe(4.5);
  expect(saved.graph.nodes.find((node) => node.id === 'host').params.ready).toBe(false);
  expect(saved.graph.nodes[0].params).not.toHaveProperty('exposedPorts');

  await page.reload();
  await expect(host.locator('[data-handleid="samples"]')).toBeVisible();
  await host.locator('.node-title').click();
  await expect(page.getByRole('checkbox', { name: '显露 exposedParameters:samples', exact: true })).toBeChecked();
  await expect(page.getByRole('checkbox', { name: '显露 exposedBulkSlots:samples', exact: true })).toBeChecked();
  await expect(panel.locator('[data-interface="parameter:samples"] .param-field input')).toHaveValue('4.5');
  await page.getByRole('checkbox', { name: '显露 exposedPorts:samples', exact: true }).uncheck();
  await expect(host.locator('[data-handleid="samples"]')).toHaveCount(0);
  await expect(host.locator('[data-handleid="out"]')).toBeVisible();
  expect(writes).toEqual([]);
  expect(errors).toEqual([]);
});
