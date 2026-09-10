const { test, expect } = require('@playwright/test');

const projectName = `e2e_alter_visual_${Date.now()}`;

const project = {
  version: '0.1.0',
  metadata: { name: projectName },
  sample_rate: 48000,
  block_size: 128,
  buffer_size: 0,
  double_bank: 'auto',
  target: 'auto',
  graph: {
    nodes: [
      {
        id: 'input_win', component: 'orpheus.builtin.device_in', alters: ['input_dsp'],
        params: { channels: 2 }, position: { x: 180, y: 180 },
      },
      {
        id: 'input_dsp', component: 'orpheus.builtin.embed_in', alters: ['input_win'],
        params: { channels: 2 }, position: { x: 460, y: 180 },
      },
      {
        id: 'portable_gain', component: 'orpheus.builtin.gain',
        params: { channels: 2, gain_db: 0 }, position: { x: 760, y: 420 },
      },
    ],
    connections: [],
  },
};

async function box(locator) {
  const result = await locator.boundingBox();
  expect(result).not.toBeNull();
  return result;
}

function contains(outer, inner) {
  return outer.x <= inner.x
    && outer.y <= inner.y
    && outer.x + outer.width >= inner.x + inner.width
    && outer.y + outer.height >= inner.y + inner.height;
}

test.beforeAll(async ({ request }) => {
  expect((await request.post('/api/projects', { data: { name: projectName } })).ok()).toBeTruthy();
  expect((await request.put(`/api/projects/${projectName}`, { data: project })).ok()).toBeTruthy();
});

test.afterAll(async ({ request }) => {
  await request.delete(`/api/projects/${projectName}`);
});

test('alter 组自动绘框并显示平台样式', async ({ page }, testInfo) => {
  await page.goto('/');
  await page.locator('.toolbar select').first().selectOption(projectName);
  await expect(page.getByText(`已打开工程 ${projectName}`)).toBeVisible();

  const frame = page.locator('.alter-group-frame');
  const winNode = page.locator('.react-flow__node').filter({ hasText: 'input_win' });
  const dspNode = page.locator('.react-flow__node').filter({ hasText: 'input_dsp' });
  const portableNode = page.locator('.react-flow__node').filter({ hasText: 'portable_gain' });

  await expect(frame).toHaveCount(1);
  await expect(frame.getByText('平台替代 · 2 选 1')).toBeVisible();
  await expect(winNode.getByText('WIN')).toBeVisible();
  await expect(dspNode.getByText('DSP')).toBeVisible();
  await expect(portableNode.locator('.platform-badge')).toHaveCount(0);

  const platformBadgeBox = await winNode.locator('.platform-badge').boundingBox();
  const infoButtonBox = await winNode.locator('.node-info-btn').boundingBox();
  expect(infoButtonBox).not.toBeNull();
  expect(platformBadgeBox.x + platformBadgeBox.width).toBeLessThanOrEqual(infoButtonBox.x + 1);

  const winColor = await winNode.locator('.orpheus-node').evaluate(
    (node) => getComputedStyle(node).borderLeftColor
  );
  const dspColor = await dspNode.locator('.orpheus-node').evaluate(
    (node) => getComputedStyle(node).borderLeftColor
  );
  expect(winColor).not.toBe(dspColor);

  const initialFrame = await box(frame);
  expect(contains(initialFrame, await box(winNode))).toBeTruthy();
  expect(contains(initialFrame, await box(dspNode))).toBeTruthy();

  const dspHeader = dspNode.locator('.node-header');
  const headerBox = await box(dspHeader);
  await page.mouse.move(headerBox.x + headerBox.width / 2, headerBox.y + 12);
  await page.mouse.down();
  await page.mouse.move(headerBox.x + headerBox.width / 2 + 140, headerBox.y + 72, { steps: 8 });
  await page.mouse.up();

  await expect.poll(async () => (await box(frame)).width).toBeGreaterThan(initialFrame.width + 80);
  const movedFrame = await box(frame);
  expect(contains(movedFrame, await box(winNode))).toBeTruthy();
  expect(contains(movedFrame, await box(dspNode))).toBeTruthy();

  await page.screenshot({ path: testInfo.outputPath('alter-platform-groups.png'), fullPage: true });
});
