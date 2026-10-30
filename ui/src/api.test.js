import axios from 'axios';
import * as api from './api';

jest.mock('axios', () => ({
  create: jest.fn(() => ({
    defaults: { baseURL: '/api' },
    get: jest.fn().mockResolvedValue({ data: {} }),
    post: jest.fn().mockResolvedValue({ data: {} }),
    put: jest.fn().mockResolvedValue({ data: {} }),
    delete: jest.fn().mockResolvedValue({ data: {} }),
  })),
}));

const client = axios.create.mock.results[0].value;

beforeEach(() => {
  for (const method of ['get', 'post', 'put', 'delete']) {
    client[method].mockResolvedValue({ data: {} });
  }
});

test.each([
  ['demo', '/projects/demo'],
  ['repo/demo', '/projects/repo%3Ademo'],
  ['repo/components', '/projects/repo%3Acomponents'],
  ['中文 repo/模型 v1.2', `/projects/${encodeURIComponent('中文 repo:模型 v1.2')}`],
  ['repo/100%2F #', '/projects/repo%3A100%252F%20%23'],
])('工程路径 %s 编码为无歧义的单个 URL 段', (name, expected) => {
  expect(api.projectPath(name)).toBe(expected);
});

test('工程操作和下载资源统一使用嵌套工程路径', async () => {
  const name = 'repo/demo';
  const prefix = '/projects/repo%3Ademo';
  const operations = [
    ['get', '', () => api.getProject(name)],
    ['put', '', () => api.saveProject(name, {})],
    ['delete', '', () => api.deleteProject(name)],
    ['post', '/distill', () => api.importDistilled(name, '')],
    ['get', '/components', () => api.listProjectComponents(name)],
    ['get', '/components/gain/readme', () => api.getComponentReadme('gain', name)],
    ['post', '/lesson/check', () => api.checkLesson(name)],
    ['get', '/notes', () => api.getProjectNotes(name)],
    ['put', '/notes', () => api.saveProjectNotes(name, '')],
    ['get', '/node_notes', () => api.getNodeNotes(name)],
    ['put', '/node_notes', () => api.saveNodeNotes(name, {})],
    ['post', '/compile', () => api.compileProject(name)],
    ['post', '/run', () => api.runProject(name, false)],
    ['post', '/run_generated', () => api.runGenerated(name)],
    ['post', '/generate', () => api.generateProject(name)],
    ['post', '/rt/start', () => api.rtStart(name)],
    ['post', '/rt/stop', () => api.rtStop(name)],
    ['get', '/rt/status', () => api.rtStatus(name)],
    ['post', '/rt/param', () => api.rtSetParam(name, 'n', 'p', 1)],
    ['post', '/rt/bulk', () => api.rtWriteBulk(name, 'n', 'k', [])],
    ['get', '/rt/resolve', () => api.rtResolve(name, 1)],
    ['get', '/rt/map', () => api.rtMap(name)],
    ['post', '/rt/write', () => api.rtWriteById(name, 1, 0)],
    ['post', '/rt/read', () => api.rtReadById(name, 1)],
    ['post', '/rt/write_bulk', () => api.rtWriteBulkById(name, 1, [])],
    ['post', '/rt/read_bulk', () => api.rtReadBulkById(name, 1)],
    ['get', '/files', () => api.listProjectFiles(name)],
    ['post', '/uploads', () => api.uploadProjectFile(name, new File(['x'], 'x.txt'))],
  ];
  for (const [method, suffix, invoke] of operations) {
    await invoke();
    expect(client[method].mock.calls.at(-1)[0]).toBe(prefix + suffix);
  }
  expect(api.downloadUrl(name)).toBe(`/api${prefix}/download`);
  expect(api.downloadGeneratedUrl(name)).toBe(`/api${prefix}/generated/archive`);
  expect(api.projectFileUrl(name, 'assets/音频 #1.wav')).toBe(
    `/api${prefix}/files/assets/${encodeURIComponent('音频 #1.wav')}`
  );
});
