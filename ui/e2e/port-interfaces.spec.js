const { test, expect } = require('./editor-test');

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
  const interfaceRequests = [];
  page.on('request', (request) => {
    if (new URL(request.url()).pathname === `/api/projects/${name}/interfaces`)
      interfaceRequests.push(request.postDataJSON().view_key);
  });

  try {
    const saved = await request.put(`/api/projects/${name}`, { data: document });
    expect(saved.ok(), await saved.text()).toBeTruthy();
    await page.route('**/api/**', async (route) => {
      const { pathname } = new URL(route.request().url());
      if (pathname.startsWith('/api/editor/')) return route.continue();
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
    await page.locator('.react-flow__node[data-id="child"]').dblclick();
    await expect(page.locator('.react-flow__node[data-id="body"]')).toContainText('f32 · 2ch · 48帧');
    const requestCount = interfaceRequests.length;
    await page.locator('.tab').filter({ hasText: '外层' }).click();
    await expect(page.locator('.react-flow__node[data-id="child"]')).toContainText('f32 · 5ch · 48帧');
    await page.locator('.tab').filter({ hasText: '主图' }).click();
    await expect(a).toContainText('f32 · 9ch · 48帧');
    await page.locator('.tab').filter({ hasText: '内层' }).click();
    await expect(page.locator('.react-flow__node[data-id="body"]')).toContainText('f32 · 2ch · 48帧');
    expect(interfaceRequests.length).toBe(requestCount);
    expect(blocked).toEqual([]);
  } finally {
    expect((await request.delete(`/api/projects/${name}`)).ok()).toBeTruthy();
  }
});

test('已配置格式直接显示，失配仅标红对应线，用户修正参数后恢复且不偷偷保存', async ({ page, request }) => {
  const name = `test_configured_ports_${Date.now()}`;
  expect((await request.post('/api/projects', { data: { name } })).status()).toBe(201);
  const document = {
    version: '0.1.0', block_size: 32, sample_rate: 48000,
    graph: {
      nodes: [
        { id: 'source', component: 'orpheus.builtin.signal_gen', params: { channels: 2 },
          position: { x: 100, y: 100 } },
        { id: 'target', component: 'orpheus.builtin.gain', params: { channels: 1 },
          position: { x: 700, y: 100 } },
        { id: 'ok', component: 'orpheus.builtin.null_sink', params: { channels: 2 },
          position: { x: 700, y: 400 } },
      ],
      connections: [{ from: 'source:out', to: 'target:in' }, { from: 'source:out', to: 'ok:in' }],
    },
  };
  let release;
  const gate = new Promise((resolve) => { release = resolve; });
  const blocked = [];
  try {
    expect((await request.put(`/api/projects/${name}`, { data: document })).ok()).toBeTruthy();
    await page.route('**/api/**', async (route) => {
      const { pathname } = new URL(route.request().url());
      if (pathname.startsWith('/api/editor/')) return route.continue();
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
      if (pathname.endsWith('/interfaces')) await gate;
      await route.continue();
    });
    await page.goto('/');
    await page.getByRole('checkbox', { name: '自动保存', exact: true }).uncheck();
    const source = page.locator('.react-flow__node[data-id="source"]');
    const target = page.locator('.react-flow__node[data-id="target"]');
    await expect(source).toContainText('配置：audio · f32 · 2ch');
    await expect(target).toContainText('配置：audio · f32 · 1ch');
    release();
    const invalid = page.locator('.audio-edge-path.mismatch');
    await expect(invalid).toHaveCount(1);
    await expect(invalid.locator('title')).toContainText('通道数：source:out（2） → target:in（1）');
    await expect(invalid.locator('.react-flow__edge-path')).toHaveCSS('stroke', 'rgb(224, 49, 49)');
    await expect(page.locator('.audio-edge-path[data-connection-status="matched"]')).toHaveCount(1);
    await target.locator('.node-title').click();
    const channels = page.locator('[data-interface="parameter:channels"] .param-field input');
    await expect(channels).toHaveValue('1');
    await channels.fill('2');
    await channels.press('Tab');
    await expect(invalid).toHaveCount(0);
    await expect(page.locator('.audio-edge-path[data-connection-status="matched"]')).toHaveCount(2);
    const stored = await (await request.get(`/api/projects/${name}`)).json();
    expect(stored.graph.nodes.find((node) => node.id === 'target').params.channels).toBe(1);
    expect(stored.graph.connections).toEqual(document.graph.connections);
    expect(blocked).toEqual([]);
  } finally {
    release();
    expect((await request.delete(`/api/projects/${name}`)).ok()).toBeTruthy();
  }
});
