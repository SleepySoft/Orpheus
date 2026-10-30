const { test, expect } = require('@playwright/test');
const fs = require('fs');
const path = require('path');

const group = `e2e_workspace_${Date.now()}`;
const source = `${group}_source`;
const name = `${group}/克隆 repo/examples/demo v1.2`;
const other = `${group}/other/demo v1.2`;
const workspace = path.resolve(__dirname, '..', '..', 'workspace');
const url = (project) => `/api/projects/${encodeURIComponent(project.replace(/\//g, ':'))}`;

test.beforeAll(async ({ request }) => {
  const response = await request.post('/api/projects', {
    data: { name: source, from_example: 'placeholder_demo' },
  });
  expect(response.ok()).toBeTruthy();
  fs.cpSync(path.join(workspace, source), path.join(workspace, ...name.split('/')), { recursive: true });
  fs.mkdirSync(path.join(workspace, group, '克隆 repo', '.git'), { recursive: true });
  expect((await request.post('/api/projects', { data: { name: other } })).ok()).toBeTruthy();
});

test.afterAll(async ({ request }) => {
  await request.delete(url(name));
  await request.delete(url(other));
  await request.delete(url(source));
  fs.rmSync(path.join(workspace, group), { recursive: true, force: true });
});

test('递归发现复制到 workspace 的工程，按完整路径打开、保存和恢复', async ({ page, request }) => {
  await page.goto('/');
  const selector = page.locator('.toolbar select').first();
  await expect(selector.locator('option', { hasText: name })).toHaveCount(1);
  await expect(selector.locator('option', { hasText: other })).toHaveCount(1);
  await selector.selectOption(name);
  await expect(page.getByText(`已打开工程 ${name}`, { exact: true })).toBeVisible();
  await expect(page.locator('.react-flow__node[data-id="pending"]')).toBeVisible();
  await page.getByRole('button', { name: 'TODO', exact: true }).click();
  await page.getByRole('button', { name: 'main/pending#implement', exact: true }).click();
  await page.locator('.rightbar').getByLabel('下一步（从哪里继续、做什么）').fill('从克隆工程继续实现');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect(page.getByText(`已保存 ${name} ·`)).toBeVisible();
  const saved = await (await request.get(url(name))).json();
  expect(saved.graph.nodes.find((node) => node.id === 'pending').todos[0].next_step).toBe('从克隆工程继续实现');
  expect((await (await request.get(url(other))).json()).graph.nodes).toEqual([]);
  await page.reload();
  await selector.selectOption(name);
  await page.getByRole('button', { name: 'TODO', exact: true }).click();
  await expect(page.getByText('下一步：从克隆工程继续实现')).toBeVisible();
  const compiled = await request.post(url(name) + '/compile');
  expect(compiled.ok()).toBeTruthy();
});

test('嵌套工程在自身目录执行，运行日志可读取', async ({ request }) => {
  test.skip(!fs.existsSync(path.resolve(workspace, '..', 'build', 'orpheus_runtime.exe')),
    '需要已构建的 Windows Runtime');
  const response = await request.post(url(name) + '/run');
  expect(response.ok()).toBeTruthy();
  const result = await response.json();
  expect(result.status, result.stderr).toBe('ok');
  expect((await request.get(url(name) + '/files/' + result.log_path)).ok()).toBeTruthy();
});
