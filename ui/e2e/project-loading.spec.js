const { test, expect } = require('@playwright/test');

const components = [{ id: 'test.leaf', name: '测试节点', ports: [], parameters: [] }];
const document = (id) => ({
  version: '1.0.0', metadata: { name: id },
  graph: {
    nodes: id ? [{ id, component: 'test.leaf', position: { x: 100, y: 100 } }] : [],
    connections: [],
  },
});

async function mockBackend(page, projectResponse) {
  await page.route('**/api/**', async (route) => {
    const path = new URL(route.request().url()).pathname;
    let data;
    if (path === '/api/projects') data = [{ name: 'first' }, { name: 'second' }];
    else if (path === '/api/components' || path.endsWith('/components')) data = components;
    else if (path === '/api/examples') data = [];
    else if (path === '/api/devices') data = { capture: [], playback: [] };
    else if (path.endsWith('/node_notes')) data = { notes: {} };
    else if (path.endsWith('/rt/status')) data = { running: false, logs: [], probes: {} };
    else if (path === '/api/projects/first' || path === '/api/projects/second') {
      await projectResponse(route, path.endsWith('/first') ? 'first' : 'second');
      return;
    } else {
      await route.fulfill({ status: 404, json: { detail: `未模拟的请求：${path}` } });
      return;
    }
    await route.fulfill({ json: data });
  });
}

test('首次慢载入持续显示阶段与用时，完成画布初始化后退出', async ({ page }) => {
  let release;
  const waiting = new Promise((resolve) => { release = resolve; });
  const errors = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await mockBackend(page, async (route) => {
    await waiting;
    await route.fulfill({ json: document('first-node') });
  });
  await page.goto('/');
  const dialog = page.getByRole('dialog', { name: '正在载入工程' });
  await expect(dialog).toBeVisible();
  await expect(dialog.locator('[aria-current="step"]')).toHaveText('2. 读取工程、组件与布局');
  await expect(dialog.getByRole('progressbar')).not.toHaveAttribute('value');
  await expect(dialog.locator('small')).not.toContainText('已用时 0 秒');
  await expect(page.locator('.app')).toHaveAttribute('inert', '');
  release();
  await expect(dialog).toHaveCount(0);
  await expect(page.getByText('已打开工程 first', { exact: true })).toBeVisible();
  await expect(page.locator('.react-flow__node[data-id="first-node"]')).toBeVisible();
  await expect(page.locator('.app')).not.toHaveAttribute('inert');
  expect(errors).toEqual([]);
});

test('切换失败解除载入状态并保留旧图，重试空图也能完成', async ({ page }) => {
  let fail = true;
  let release;
  const waiting = new Promise((resolve) => { release = resolve; });
  const errors = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await mockBackend(page, async (route, name) => {
    if (name === 'second' && fail) {
      await route.fulfill({ status: 500, json: { detail: '组件定义读取失败' } });
    } else {
      if (name === 'second') await waiting;
      await route.fulfill({ json: document(name === 'first' ? 'first-node' : null) });
    }
  });
  await page.goto('/');
  await expect(page.getByText('已打开工程 first', { exact: true })).toBeVisible();
  const selector = page.locator('.toolbar select').first();
  await selector.selectOption('second');
  await expect(page.getByText('打开工程失败: 组件定义读取失败', { exact: true })).toBeVisible();
  await expect(page.getByRole('dialog', { name: '正在载入工程' })).toHaveCount(0);
  await expect(page.locator('.react-flow__node[data-id="first-node"]')).toBeVisible();
  await page.locator('.react-flow__node[data-id="first-node"]').click();
  fail = false;
  await selector.selectOption('second');
  await expect(page.getByRole('dialog', { name: '正在载入工程' })).toBeVisible();
  await page.keyboard.press('Delete');
  await expect(page.locator('.react-flow__node[data-id="first-node"]')).toBeVisible();
  release();
  await expect(page.getByText('已打开工程 second', { exact: true })).toBeVisible();
  await expect(page.getByRole('dialog', { name: '正在载入工程' })).toHaveCount(0);
  await expect(page.locator('.react-flow__node')).toHaveCount(0);
  expect(errors).toEqual([]);
});
