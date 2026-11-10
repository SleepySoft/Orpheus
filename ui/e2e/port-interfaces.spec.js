const { test, expect } = require('@playwright/test');

test.use({ viewport: { width: 1600, height: 1000 } });

test('真实 API 递归解析实例端口，编辑快照不落盘且定义视图使用自身默认值', async ({ page, request }) => {
  const name = `test_port_interfaces_${Date.now()}`;
  expect((await request.post('/api/projects', { data: { name } })).status()).toBe(201);
  const document = {
    version: '0.1.0', block_size: 48, sample_rate: 32000,
    graph: { nodes: [
      { id: 'a', component: 'test.port_outer', params: { width: 7 }, position: { x: 100, y: 100 } },
      { id: 'b', component: 'test.port_outer', params: { width: 3 }, position: { x: 700, y: 100 } },
    ], connections: [] },
    layout: { version: 1, views: {}, node_ui: { main: {
      a: { exposedPorts: ['in', 'out'] }, b: { exposedPorts: ['in', 'out'] },
    } } },
    subcomponents: [{
      id: 'test.port_inner', name: '内层',
      ports: [{ id: 'in', direction: 'input', maps_to: 'body:in' },
        { id: 'out', direction: 'output', maps_to: 'body:out' }],
      public_parameters: [{ id: 'channels', type: 'int', default: 2, direction: 'input',
        maps_to: 'body:channels_in', update_policy: 'restart_required' }],
      graph: { nodes: [{ id: 'body', component: 'orpheus.builtin.channel_router',
        params: { channels_in: 2, channels_out: 1, select: '1' } }], connections: [] },
    }, {
      id: 'test.port_outer', name: '外层',
      ports: [{ id: 'in', direction: 'input', maps_to: 'child:in' },
        { id: 'out', direction: 'output', maps_to: 'child:out' }],
      public_parameters: [{ id: 'width', type: 'int', default: 5, direction: 'input',
        maps_to: 'child:channels', update_policy: 'restart_required' }],
      graph: { nodes: [{ id: 'child', component: 'test.port_inner' }], connections: [] },
    }],
  };
  const blocked = [];
  try {
    const saved = await request.put(`/api/projects/${name}`, { data: document });
    expect(saved.ok(), await saved.text()).toBeTruthy();
    await page.route('**/api/**', async (route) => {
      const { pathname } = new URL(route.request().url());
      const method = route.request().method();
      if (pathname === '/api/projects' && method === 'GET') {
        await route.fulfill({ json: [{ name }] });
        return;
      }
      const own = pathname === `/api/projects/${name}` || pathname.startsWith(`/api/projects/${name}/`);
      if ((pathname.startsWith('/api/projects/') && !own)
        || (!['GET', 'HEAD'].includes(method) && (!own || !pathname.endsWith('/interfaces')))) {
        blocked.push(`${method} ${pathname}`);
        await route.abort();
        return;
      }
      await route.continue();
    });
    await page.goto('/');
    await page.getByRole('checkbox', { name: '自动保存', exact: true }).uncheck();
    const a = page.locator('.react-flow__node[data-id="a"]');
    const b = page.locator('.react-flow__node[data-id="b"]');
    await expect(a).toContainText('f32 · 7ch · 48帧 · 32000Hz');
    await expect(a).toContainText('f32 · 1ch · 48帧 · 32000Hz');
    await expect(b).toContainText('f32 · 3ch · 48帧 · 32000Hz');
    await a.locator('.node-title').click();
    await expect(page.locator('[data-interface="port:in"]')).toContainText('a__child__body:in');
    const width = page.locator('[data-interface="parameter:width"] .param-field input');
    await width.fill('9');
    await width.press('Tab');
    await expect(a).toContainText('f32 · 9ch · 48帧');
    await expect(b).toContainText('f32 · 3ch · 48帧');
    const stored = await (await request.get(`/api/projects/${name}`)).json();
    expect(stored.graph.nodes.find((node) => node.id === 'a').params.width).toBe(7);
    await a.dblclick();
    await expect(page.locator('.react-flow__node[data-id="child"]')).toContainText('f32 · 5ch · 48帧');
    expect(blocked).toEqual([]);
  } finally {
    expect((await request.delete(`/api/projects/${name}`)).ok()).toBeTruthy();
  }
});
