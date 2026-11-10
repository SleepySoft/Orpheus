const { test, expect } = require('@playwright/test');

test.use({ viewport: { width: 1600, height: 1000 } });

test('两页面只允许一个写者，接管刷新、保留失权页未保存内容及关闭释放', async ({ browser, request }) => {
  const name = `test_edit_lease_${Date.now()}`;
  const headersFor = async () => {
    const response = await request.post('/api/editor/acquire', { data: { takeover: true } });
    expect(response.ok(), await response.text()).toBeTruthy();
    return { 'X-Orpheus-Edit-Token': (await response.json()).token };
  };
  const setupHeaders = await headersFor();
  const contexts = [];
  try {
    expect((await request.post('/api/projects', { data: { name }, headers: setupHeaders })).status()).toBe(201);
    const document = {
      version: '0.1.0', block_size: 32, sample_rate: 48000,
      graph: { nodes: [{ id: 'gain', component: 'orpheus.builtin.gain',
        params: { gain_db: 1, channels: 1 }, position: { x: 200, y: 120 } }], connections: [] },
    };
    expect((await request.put(`/api/projects/${name}`, { data: document, headers: setupHeaders })).ok()).toBeTruthy();
    expect((await request.post('/api/editor/release', { headers: setupHeaders })).ok()).toBeTruthy();
    const open = async () => {
      const context = await browser.newContext({ viewport: { width: 1600, height: 1000 } });
      contexts.push(context);
      const page = await context.newPage();
      await page.route('**/api/projects', (route) =>
        route.request().method() === 'GET' ? route.fulfill({ json: [{ name }] }) : route.continue());
      return page;
    };
    const a = await open();
    const grant = a.waitForResponse((response) =>
      response.url().endsWith('/api/editor/acquire') && response.status() === 200);
    await a.goto('/');
    const oldToken = (await (await grant).json()).token;
    await expect(a.locator('.edit-lease-status')).toContainText('可编辑');
    await a.locator('.react-flow__node[data-id="gain"] .node-title').click();
    const gainA = a.locator('[data-interface="parameter:gain_db"] .param-field input');
    await expect(gainA).toHaveValue('1');
    await gainA.fill('5');
    await gainA.press('Tab');
    await expect.poll(async () => {
      const stored = await (await request.get(`/api/projects/${name}`)).json();
      return stored.graph.nodes[0].params.gain_db;
    }).toBe(5);
    await a.getByRole('checkbox', { name: '自动保存', exact: true }).uncheck();
    await gainA.fill('9');
    await gainA.press('Tab');
    const popupEvent = a.waitForEvent('popup');
    await a.getByRole('button', { name: '独立窗口', exact: true }).click();
    const popup = await popupEvent;
    const popupGain = popup.locator('[data-interface="parameter:gain_db"] .param-field input');
    await expect(popupGain).toHaveValue('9');
    const b = await open();
    await b.goto('/');
    await expect(b.locator('.edit-lease-status')).toContainText('只读');
    await expect(b.getByRole('button', { name: '新建工程', exact: true })).toBeDisabled();
    expect((await request.put(`/api/projects/${name}/node_notes`, {
      data: { notes: { gain: '只读页打开后才保存的新笔记' } },
      headers: { 'X-Orpheus-Edit-Token': oldToken },
    })).ok()).toBeTruthy();
    b.on('dialog', (dialog) => dialog.accept());
    await b.getByRole('button', { name: '接管并刷新', exact: true }).click();
    await expect(b.locator('.edit-lease-status')).toContainText('可编辑');
    await b.locator('.react-flow__node[data-id="gain"] .node-title').click();
    const gainB = b.locator('[data-interface="parameter:gain_db"] .param-field input');
    await expect(gainB).toHaveValue('5');
    await expect(b.locator('.rightbar .node-notes-textarea')).toHaveValue('只读页打开后才保存的新笔记');
    await a.keyboard.press('Control+s');
    await expect(a.locator('.edit-lease-status')).toContainText('只读');
    await expect(popupGain).toBeDisabled();
    await expect(popupGain).toHaveValue('9');
    for (const operation of ['renew', 'release']) {
      expect((await request.post(`/api/editor/${operation}`, {
        headers: { 'X-Orpheus-Edit-Token': oldToken },
      })).status()).toBe(409);
    }
    expect((await request.put(`/api/projects/${name}`, {
      data: document, headers: { 'X-Orpheus-Edit-Token': oldToken },
    })).status()).toBe(409);
    await gainB.fill('7');
    await gainB.press('Tab');
    await expect.poll(async () =>
      (await (await request.get(`/api/projects/${name}`)).json()).graph.nodes[0].params.gain_db
    ).toBe(7);
    await b.close();
    await expect.poll(async () =>
      (await (await request.get('/api/editor/lease')).json()).occupied
    ).toBe(false);
    a.on('dialog', (dialog) => dialog.accept());
    await a.getByRole('button', { name: '接管并刷新', exact: true }).click();
    await expect(a.locator('.edit-lease-status')).toContainText('可编辑');
    await a.locator('.react-flow__node[data-id="gain"] .node-title').click();
    await expect(popup.locator('[data-interface="parameter:gain_db"] .param-field input')).toHaveValue('7');
  } finally {
    for (const context of contexts) await context.close();
    const cleanupHeaders = await headersFor();
    expect((await request.delete(`/api/projects/${name}`, { headers: cleanupHeaders })).ok()).toBeTruthy();
    await request.post('/api/editor/release', { headers: cleanupHeaders });
  }
});
