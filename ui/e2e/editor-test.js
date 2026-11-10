const { test: base, expect } = require('@playwright/test');

// 业务用例的 API 准备/清理也是独立写者；显式接管并及时释放，不绕过服务端保护。
const test = base.extend({
  _workspaceLeaseCleanup: [async ({ request }, use) => {
    try {
      await use();
    } finally {
      const acquired = await request.post('/api/editor/acquire', { data: { takeover: true } });
      expect(acquired.ok(), await acquired.text()).toBeTruthy();
      const headers = { 'X-Orpheus-Edit-Token': (await acquired.json()).token };
      expect((await request.post('/api/editor/release', { headers })).ok()).toBeTruthy();
    }
  }, { auto: true }],
  request: async ({ request }, use) => {
    const client = new Proxy(request, {
      get(target, key) {
        const method = target[key];
        if (typeof method !== 'function') return method;
        if (!['post', 'put', 'patch', 'delete'].includes(key)) return method.bind(target);
        return async (url, options = {}) => {
          if (url.includes('/editor/') || /\/(interfaces|lesson\/check|rt\/read|rt\/read_bulk)$/.test(url))
            return method.call(target, url, options);
          const acquired = await target.post('/api/editor/acquire', { data: { takeover: true } });
          expect(acquired.ok(), await acquired.text()).toBeTruthy();
          const token = (await acquired.json()).token;
          const headers = { ...options.headers, 'X-Orpheus-Edit-Token': token };
          try {
            return await method.call(target, url, { ...options, headers });
          } finally {
            const released = await target.post('/api/editor/release', { headers });
            expect(released.ok(), await released.text()).toBeTruthy();
          }
        };
      },
    });
    await use(client);
  },
});

module.exports = { test, expect };
