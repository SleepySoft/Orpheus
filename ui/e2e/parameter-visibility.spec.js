const { test, expect } = require('@playwright/test');

test.use({ viewport: { width: 1600, height: 1000 } });

const identity = Array.from({ length: 30 }, (_, index) => index + 1).join(',');
const component = {
  id: 'test.router', name: 'Router',
  parameters: [
    { id: 'channels_in', name: '输入通道数', type: 'int', default: 30, affects_signature: true, update_policy: 'restart_required' },
    { id: 'channels_out', name: '输出通道数', type: 'int', default: 30, affects_signature: true, update_policy: 'restart_required' },
    { id: 'select', name: '选择映射', type: 'string', default: identity, update_policy: 'restart_required' },
    { id: 'override_select', name: '运行期覆盖映射', type: 'string', default: '', update_policy: 'block_boundary' },
  ],
  ports: [
    { id: 'in', type: 'audio', direction: 'input', sample_format: 'f32', channels: 'param:channels_in' },
    { id: 'out', type: 'audio', direction: 'output', sample_format: 'f32', channels: 'param:channels_out' },
  ],
};
const initial = {
  version: '1.0.0', metadata: { name: 'parameter-visibility-preview' }, block_size: 32,
  graph: {
    nodes: [{ id: 'router_instance', component: 'sub:router', position: { x: 100, y: 100 } }],
    connections: [],
  },
  subcomponents: [{
    id: 'router', name: '输入前级', read_only: true,
    ports: [
      { ...component.ports[0], channels: 30, maps_to: 'body:in' },
      { ...component.ports[1], channels: 30, maps_to: 'body:out' },
    ],
    public_parameters: [
      { ...component.parameters[2], id: 'body_select', maps_to: 'body:select', direction: 'input' },
      { ...component.parameters[3], id: 'body_override_select', maps_to: 'body:override_select', direction: 'input' },
    ],
    graph: {
      nodes: [{
        id: 'body', component: component.id, label: 'Router：映射覆盖与非法通道置零（30→30）',
        params: { channels_in: 30, channels_out: 30, select: identity, override_select: '' },
        position: { x: 100, y: 100 },
      }],
      connections: [],
    },
  }],
};

test('Router 初始化/运行期公开参数默认隐藏，显露不删映射且保存重载', async ({ page }) => {
  let document = initial;
  let saved;
  const errors = [];
  const unexpectedWrites = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.context().route('**/api/**', async (route) => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    if (request.method() === 'POST' && path.endsWith('/interfaces')) {
      const { view_key: viewKey } = request.postDataJSON();
      const nodeId = viewKey === 'main' ? 'router_instance' : 'body';
      await route.fulfill({ json: { view_key: viewKey, diagnostics: [], nodes: {
        [nodeId]: { status: 'resolved', ports: component.ports.map((port) => ({
          ...port, channels: 30, frames: 32, sample_rate: 48000,
          resolution: { status: 'resolved', endpoint: 'router_instance__body:' + port.id, component: component.id },
        })) },
      } } });
      return;
    }
    if (request.method() === 'PUT' && path === '/api/projects/parameter-visibility-preview') {
      saved = request.postDataJSON();
      document = saved;
      await route.fulfill({ json: saved });
      return;
    }
    if (request.method() !== 'GET') {
      unexpectedWrites.push(path);
      await route.abort();
      return;
    }
    let data;
    if (path === '/api/projects') data = [{ name: 'parameter-visibility-preview' }];
    else if (path === '/api/components' || path.endsWith('/components')) data = [component];
    else if (path === '/api/examples') data = [];
    else if (path === '/api/devices') data = { capture: [], playback: [] };
    else if (path.endsWith('/node_notes')) data = { notes: {} };
    else if (path.endsWith('/rt/status')) data = { running: false, logs: [], probes: {} };
    else if (path === '/api/projects/parameter-visibility-preview') data = document;
    else { await route.fulfill({ status: 404, json: { detail: `未模拟：${path}` } }); return; }
    await route.fulfill({ json: data });
  });
  await page.goto('/');
  await page.getByRole('checkbox', { name: '自动保存', exact: true }).uncheck();
  await page.locator('.react-flow__node[data-id="router_instance"]').dblclick();
  const router = page.locator('.react-flow__node[data-id="body"]');
  await expect(router).toContainText('Router：映射覆盖与非法通道置零（30→30）');
  await expect(router.locator('.node-controls, .node-initial-config')).toHaveCount(0);
  await expect(router.locator('[data-handleid^="ctl:"]')).toHaveCount(0);
  await router.locator('.node-title').click();
  const panel = page.locator('.sidebar');
  await expect(panel.locator('[data-interface^="parameter:"]')).toHaveCount(4);
  await expect(panel.getByText('初始化配置（3）', { exact: true })).toBeVisible();
  await expect(panel.getByText('运行期参数（1）', { exact: true })).toBeVisible();
  const initialExposure = page.getByRole('checkbox', { name: '显露 exposedParameters:select', exact: true });
  const runtimeExposure = page.getByRole('checkbox', { name: '显露 exposedParameters:override_select', exact: true });
  await expect(initialExposure).toBeEnabled();
  await expect(initialExposure).not.toBeChecked();
  await expect(runtimeExposure).toBeEnabled();
  await expect(runtimeExposure).not.toBeChecked();
  await initialExposure.check();
  await expect(router.locator('.initial-config-row[data-parameter="select"]')).toBeVisible();
  await expect(router.locator('.export-control-pin, [data-handleid="ctl:select"]')).toHaveCount(0);
  await expect(panel.locator('[data-interface="parameter:select"]')).toContainText('启动前注入');
  await initialExposure.uncheck();
  await expect(router.locator('.node-initial-config')).toHaveCount(0);
  await runtimeExposure.check();
  await expect(router.locator('.ctl-row[data-parameter="override_select"]')).toBeVisible();
  await expect(router.locator('[data-handleid="ctl:override_select"]')).toHaveCount(0);
  await expect(page.locator('.control-export-row')).toHaveCount(2);
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect.poll(() => saved).toBeTruthy();
  expect(saved.subcomponents[0].public_parameters).toEqual(initial.subcomponents[0].public_parameters);
  expect(saved.subcomponents[0].read_only).toBe(true);
  expect(saved.subcomponents[0].graph.nodes[0].params).toEqual(initial.subcomponents[0].graph.nodes[0].params);
  expect(saved.layout.node_ui['sub:router'].body.exposedParameters).toEqual(['override_select']);
  await page.reload();
  await page.locator('.react-flow__node[data-id="router_instance"]').dblclick();
  await expect(router.locator('.node-initial-config')).toHaveCount(0);
  await expect(router.locator('.ctl-row[data-parameter="override_select"]')).toBeVisible();
  await router.locator('.node-title').click();
  await runtimeExposure.uncheck();
  await expect(router.locator('.node-controls')).toHaveCount(0);
  await expect(page.locator('.control-export-row')).toHaveCount(2);
  expect(errors).toEqual([]);
  expect(unexpectedWrites).toEqual([]);
});
