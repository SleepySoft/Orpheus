const { defineConfig } = require('@playwright/test');

const python = process.env.ORPHEUS_PYTHON || 'python';
const quotedPython = python.includes(' ') ? `"${python}"` : python;
const port = process.env.ORPHEUS_E2E_PORT || '8000';
const baseURL = `http://127.0.0.1:${port}`;

module.exports = defineConfig({
  testDir: './e2e',
  timeout: 30000,
  workers: 1,
  use: {
    baseURL,
    trace: 'retain-on-failure',
  },
  webServer: {
    command: `${quotedPython} serve.py --port ${port}`,
    cwd: '..',
    url: `${baseURL}/api/projects`,
    reuseExistingServer: true,
    timeout: 120000,
  },
});
