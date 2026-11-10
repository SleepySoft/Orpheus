const { test, expect } = require('./editor-test');

test.use({ viewport: { width: 1600, height: 1000 } });

const audioPorts = [
  { id: 'in', type: 'audio', direction: 'input', channels: 'param:channels' },
  { id: 'out', type: 'audio', direction: 'output', channels: 'param:channels' },
];
const components = [
  { id: 'orpheus.builtin.task_source', name: '纯任务驱动源', clock_source: true,
    ports: [], parameters: [{ id: 'frequency_hz', type: 'int', default: 1500 }] },
  { id: 'orpheus.builtin.host_audio', name: '宿主音频缓冲接口', ports: audioPorts,
    parameters: [{ id: 'channels', type: 'int', default: 2 }, { id: 'mode', type: 'int', default: 0 }] },
  { id: 'orpheus.builtin.gain', name: '增益', ports: audioPorts,
    parameters: [{ id: 'channels', type: 'int', default: 2 }] },
];
const document = {
  version: '1.0.0', metadata: { name: 'drive-buffer-preview' },
  flow_mode: 'task_sequence', block_size: 32, sample_rate: 48000,
  graph: {
    nodes: [
      { id: 'clock', component: 'orpheus.builtin.task_source',
        params: { frequency_hz: 1500 }, position: { x: 100, y: 100 } },
      { id: 'host_in', component: 'orpheus.builtin.host_audio',
        params: { channels: 30, mode: 0 }, position: { x: 600, y: 100 } },
      { id: 'gain', component: 'orpheus.builtin.gain',
        params: { channels: 30 }, position: { x: 1100, y: 100 } },
      { id: 'host_out', component: 'orpheus.builtin.host_audio',
        params: { channels: 22, mode: 1 }, position: { x: 600, y: 400 } },
    ],
    connections: [],
  },
  drive_connections: [],
};

async function dragBetween(page, source, target) {
  const from = await source.boundingBox();
  const to = await target.boundingBox();
  expect(from).not.toBeNull();
  expect(to).not.toBeNull();
  await page.mouse.move(from.x + from.width / 2, from.y + from.height / 2);
  await page.mouse.down();
  await page.mouse.move(to.x + to.width / 2, to.y + to.height / 2, { steps: 20 });
  await page.mouse.up();
}

test('源本体连接普通组件本体，退出后仍可连接PCM，保存两种独立声明', async ({ page }) => {
  let saved;
  const errors = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.route('**/api/**', async (route) => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    if (path.startsWith('/api/editor/')) return route.continue();
    if (request.method() === 'PUT' && path === '/api/projects/drive-buffer-preview') {
      saved = request.postDataJSON();
      await route.fulfill({ json: saved });
      return;
    }
    if (request.method() !== 'GET') {
      await route.abort();
      return;
    }
    let data;
    if (path === '/api/projects') data = [{ name: 'drive-buffer-preview' }];
    else if (path === '/api/components' || path.endsWith('/components')) data = components;
    else if (path === '/api/examples') data = [];
    else if (path === '/api/devices') data = { capture: [], playback: [] };
    else if (path.endsWith('/node_notes')) data = { notes: {} };
    else if (path.endsWith('/rt/status')) data = { running: false, logs: [], probes: {} };
    else if (path === '/api/projects/drive-buffer-preview') data = document;
    else {
      await route.fulfill({ status: 404, json: { detail: `未模拟的请求：${path}` } });
      return;
    }
    await route.fulfill({ json: data });
  });
  await page.goto('/');
  await expect(page.getByRole('dialog', { name: '正在载入工程' })).toHaveCount(0);
  await page.getByRole('checkbox', { name: '自动保存', exact: true }).uncheck();
  const clock = page.locator('.react-flow__node[data-id="clock"]');
  const input = page.locator('.react-flow__node[data-id="host_in"]');
  const output = page.locator('.react-flow__node[data-id="host_out"]');
  const gain = page.locator('.react-flow__node[data-id="gain"]');
  await expect(clock).toContainText('先勾选工具栏「驱动连线」');
  await expect(input.locator('.audio-port-row')).toHaveCount(0);
  await input.locator('.node-title').click();
  await page.getByRole('checkbox', { name: '显露 exposedPorts:out', exact: true }).check();
  await output.locator('.node-title').click();
  await page.getByRole('checkbox', { name: '显露 exposedPorts:in', exact: true }).check();
  await gain.locator('.node-title').click();
  await page.getByRole('checkbox', { name: '显露 exposedPorts:in', exact: true }).check();
  await expect(input.locator('.source-buffer-summary')).toContainText('输入 Buffer');
  await expect(input.locator('.source-buffer-summary')).toContainText('30ch × 32帧');
  await expect(output.locator('.source-buffer-summary')).toContainText('输出 Buffer');
  await expect(output.locator('.source-buffer-summary')).toContainText('22ch × 32帧');
  await expect(input).toContainText('独立驱动');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect.poll(() => saved).toBeTruthy();
  await expect(page.locator('.status')).toContainText('已保存 drive-buffer-preview');
  saved = undefined;

  await page.getByRole('checkbox', { name: '驱动连线', exact: true }).check();
  await expect(clock).toContainText('拖向目标组件本体，松手连接');
  const source = clock.locator('.drive-source-marker');
  const pane = await page.locator('.react-flow__pane').boundingBox();
  const box = await source.boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(pane.x + 15, pane.y + 15, { steps: 20 });
  await page.mouse.up();
  await expect(page.locator('.react-flow__edge')).toHaveCount(0);
  await expect(page.getByText('驱动入口未连接：请拖向可执行组件本体', { exact: true })).toBeVisible();

  await dragBetween(page, source, input.locator('.node-title'));
  await expect(page.locator('.react-flow__edge-drive')).toHaveCount(1);
  await expect(input.locator('.drive-domain-dot')).toHaveCount(1);
  await page.getByRole('checkbox', { name: '驱动连线', exact: true }).uncheck();
  await dragBetween(page, input.locator('[data-handleid="out"]'), gain.locator('[data-handleid="in"]'));
  await expect(page.locator('.react-flow__edge')).toHaveCount(2);
  await expect(page.locator('.react-flow__edge-drive')).toHaveCount(1);
  await expect(gain.locator('.drive-domain-dot')).toHaveCount(1);
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect.poll(() => saved).toBeTruthy();
  expect(saved.drive_connections).toEqual([{ from: 'clock:out', to: 'host_in:in' }]);
  expect(saved.graph.connections).toEqual([{ from: 'host_in:out', to: 'gain:in' }]);
  expect(errors).toEqual([]);
});
