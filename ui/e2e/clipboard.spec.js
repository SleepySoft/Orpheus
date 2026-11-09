const { test, expect } = require('@playwright/test');

const component = { id: 'test.copyable', name: '测试组件类型', ports: [], parameters: [] };
const document = {
  version: '1.0.0', metadata: { name: 'clipboard-preview' },
  graph: { nodes: [{ id: 'copy_node', component: component.id, label: '可复制显示名',
    position: { x: 100, y: 100 } }], connections: [] },
};

test('字段文字复制不被节点快捷键截获，整节点复制粘贴仍正常', async ({ page }) => {
  await page.route('**/api/**', async (route) => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    if (request.method() !== 'GET') {
      await route.abort();
      return;
    }
    let data;
    if (path === '/api/projects') data = [{ name: 'clipboard-preview' }];
    else if (path === '/api/components' || path.endsWith('/components')) data = [component];
    else if (path === '/api/examples') data = [];
    else if (path === '/api/devices') data = { capture: [], playback: [] };
    else if (path.endsWith('/node_notes')) data = { notes: {} };
    else if (path.endsWith('/rt/status')) data = { running: false, logs: [], probes: {} };
    else if (path === '/api/projects/clipboard-preview') data = document;
    else {
      await route.fulfill({ status: 404, json: { detail: `未模拟的请求：${path}` } });
      return;
    }
    await route.fulfill({ json: data });
  });
  await page.goto('/');
  await expect(page.getByRole('dialog', { name: '正在载入工程' })).toHaveCount(0);
  await page.getByRole('checkbox', { name: '自动保存', exact: true }).uncheck();
  const node = page.locator('.react-flow__node[data-id="copy_node"]');
  await node.click();
  await page.evaluate(() => {
    window.copiedTexts = [];
    document.addEventListener('copy', (event) => {
      const active = document.activeElement;
      const text = active instanceof HTMLInputElement
        ? active.value.slice(active.selectionStart, active.selectionEnd)
        : window.getSelection().toString();
      window.copiedTexts.push(text);
      // 验证原生复制事件，不改写测试机的系统剪贴板。
      event.preventDefault();
    });
    const range = document.createRange();
    range.selectNodeContents(document.querySelector('.sidebar h3'));
    window.getSelection().removeAllRanges();
    window.getSelection().addRange(range);
  });
  await page.keyboard.press('Control+c');
  await expect.poll(() => page.evaluate(() => window.copiedTexts)).toEqual(['参数面板']);
  for (const [name, value] of [
    ['节点 ID', 'copy_node'], ['显示名', '可复制显示名'], ['组件类型', component.id],
  ]) {
    const field = page.getByRole('textbox', { name, exact: true });
    await expect(field).toHaveAttribute('readonly', '');
    await field.click();
    await page.keyboard.press('Control+c');
    await expect.poll(() => page.evaluate(() => window.copiedTexts.at(-1))).toBe(value);
  }
  await node.click();
  await expect(node).toBeFocused();
  expect(await page.evaluate(() => window.getSelection().isCollapsed)).toBe(true);
  await page.keyboard.press('Control+c');
  expect(await page.evaluate(() => window.copiedTexts)).toHaveLength(4);
  await page.keyboard.press('Control+v');
  await expect(page.locator('.react-flow__node')).toHaveCount(2);
});
