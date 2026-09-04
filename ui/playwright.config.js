const { defineConfig } = require('@playwright/test');

const python = process.env.ORPHEUS_PYTHON || 'python';
const quotedPython = python.includes(' ') ? `"${python}"` : python;

module.exports = defineConfig({
  testDir: './e2e',
  timeout: 30000,
  use: {
    baseURL: 'http://127.0.0.1:8000',
    trace: 'retain-on-failure',
  },
  webServer: {
    command: `${quotedPython} serve.py`,
    cwd: '..',
    url: 'http://127.0.0.1:8000/api/projects',
    reuseExistingServer: true,
    timeout: 120000,
  },
});
