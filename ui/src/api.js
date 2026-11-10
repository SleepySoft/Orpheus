import axios from 'axios';

// Dev mode (CRA on :3000) talks to the API on :8000 via CORS; when the UI is
// hosted by the backend itself (single-command mode) use same-origin /api.
const api = axios.create({
  baseURL:
    process.env.REACT_APP_ORPHEUS_API ||
    (window.location.port === '3000' ? 'http://localhost:8000/api' : '/api'),
});

const unwrap = (p) => p.then((r) => r.data);
let editToken = null;
let editDeadline = 0;
const leaseListeners = new Set();
export const setEditToken = (token, seconds = 30) => {
  editToken = token;
  editDeadline = token ? Date.now() + seconds * 1000 : 0;
};
export const getEditToken = () => Date.now() < editDeadline ? editToken : null;
export const onEditLeaseLost = (listener) => {
  leaseListeners.add(listener);
  return () => leaseListeners.delete(listener);
};
api.interceptors.request.use((config) => {
  const token = config.editToken !== undefined ? config.editToken : getEditToken();
  if (token) config.headers.set('X-Orpheus-Edit-Token', token);
  return config;
});
api.interceptors.response.use((response) => response, (error) => {
  const token = error.config?.headers?.get?.('X-Orpheus-Edit-Token');
  if (error.response?.data?.code === 'edit_lease_lost' && token && token === editToken) {
    leaseListeners.forEach((listener) => listener(error.response.data.detail));
  }
  return Promise.reject(error);
});
export const acquireEditing = (takeover = false) =>
  unwrap(api.post('/editor/acquire', { takeover }, { timeout: 5000 }));
export const renewEditing = (token) =>
  unwrap(api.post('/editor/renew', null, { editToken: token, timeout: 5000 }));
export const releaseEditing = (token, keepalive = false) => keepalive
  ? fetch(`${api.defaults.baseURL}/editor/release`, {
    method: 'POST', headers: { 'X-Orpheus-Edit-Token': token }, keepalive: true,
  }).then((response) => {
    if (!response.ok && response.status !== 409) throw new Error(`释放编辑权失败：${response.status}`);
  })
  : unwrap(api.post('/editor/release', null, { editToken: token, timeout: 5000 }));

// 冒号在工程目录名中非法，可无歧义地将嵌套路径编码为一个 URL 段。
export const projectPath = (name) => `/projects/${encodeURIComponent(name.replace(/\//g, ':'))}`;

export const listComponents = (config) => unwrap(api.get('/components', config));
export const listProjectComponents = (name, config) =>
  unwrap(api.get(`${projectPath(name)}/components`, config));
const sourceComponentPath = (name, pkg) =>
  `${projectPath(name)}/source-components/${encodeURIComponent(pkg)}`;
export const readSourceComponent = (name, pkg) =>
  unwrap(api.get(sourceComponentPath(name, pkg)));
export const saveSourceComponent = (name, pkg, configuration, expectedSha256, token = getEditToken()) =>
  unwrap(api.put(sourceComponentPath(name, pkg), {
    configuration, expected_sha256: expectedSha256,
  }, { editToken: token }));
export const runSourceComponent = (name, pkg, operation, sha256, token = getEditToken()) =>
  unwrap(api.post(`${sourceComponentPath(name, pkg)}/${operation}`,
    { expected_sha256: sha256 }, { editToken: token }));
export const rescanComponents = () => unwrap(api.post('/components/rescan'));
export const getComponentReadme = (id, project = null) =>
  api
    .get(
      project
        ? `${projectPath(project)}/components/${id}/readme`
        : `/components/${id}/readme`,
      { responseType: 'text' }
    )
    .then((r) => r.data);
export const deleteComponent = (id) => unwrap(api.delete(`/components/${id}`));
export const promoteComponent = (id) => unwrap(api.post(`/components/${id}/promote`));
export const listProjects = (config) => unwrap(api.get('/projects', config));
export const listExamples = (config) => unwrap(api.get('/examples', config));
export const createProject = (name, fromExample = null) =>
  unwrap(api.post('/projects', { name, from_example: fromExample }));
export const importDistilled = (name, yamlText) =>
  unwrap(api.post(`${projectPath(name)}/distill`, { yaml: yamlText }));
export const getProject = (name, config) => unwrap(api.get(projectPath(name), config));
export const saveProject = (name, doc, token = getEditToken()) =>
  unwrap(api.put(projectPath(name), doc, { editToken: token }));
export const resolveProjectInterfaces = (name, document, viewKey, config) =>
  unwrap(api.post(`${projectPath(name)}/interfaces`, { document, view_key: viewKey }, config));
export const checkLesson = (name) => unwrap(api.post(`${projectPath(name)}/lesson/check`));
export const getProjectNotes = (name) => unwrap(api.get(`${projectPath(name)}/notes`));
export const saveProjectNotes = (name, content) =>
  unwrap(api.put(`${projectPath(name)}/notes`, { content }));
export const getNodeNotes = (name, config) => unwrap(api.get(`${projectPath(name)}/node_notes`, config));
export const saveNodeNotes = (name, notes) =>
  unwrap(api.put(`${projectPath(name)}/node_notes`, { notes }));
export const deleteProject = (name) => unwrap(api.delete(projectPath(name)));
export const compileProject = (name) => unwrap(api.post(`${projectPath(name)}/compile`));
export const runProject = (name, pace) =>
  unwrap(api.post(`${projectPath(name)}/run`, null, { params: { pace: pace ? 1 : 0 } }));
export const runGenerated = (name) => unwrap(api.post(`${projectPath(name)}/run_generated`));
export const generateProject = (name) => unwrap(api.post(`${projectPath(name)}/generate`));
export const listDevices = () => unwrap(api.get('/devices'));
export const rtStart = (name, opts) => unwrap(api.post(`${projectPath(name)}/rt/start`, opts || {}));
export const getLinkPorts = () => unwrap(api.get('/link/ports'));
export const rtStop = (name) => unwrap(api.post(`${projectPath(name)}/rt/stop`));
export const rtStatus = (name) => unwrap(api.get(`${projectPath(name)}/rt/status`));
export const rtSetParam = (name, node, param, value) =>
  unwrap(api.post(`${projectPath(name)}/rt/param`, { node, param, value }));
export const rtWriteBulk = (name, node, key, values) =>
  unwrap(api.post(`${projectPath(name)}/rt/bulk`, { node, key, values }));
export const rtResolve = (name, id) =>
  unwrap(api.get(`${projectPath(name)}/rt/resolve`, { params: { id } }));
export const rtMap = (name) => unwrap(api.get(`${projectPath(name)}/rt/map`));
export const rtWriteById = (name, id, value) =>
  unwrap(api.post(`${projectPath(name)}/rt/write`, { id, value }));
export const rtReadById = (name, id) =>
  unwrap(api.post(`${projectPath(name)}/rt/read`, { id }));
export const rtWriteBulkById = (name, id, values) =>
  unwrap(api.post(`${projectPath(name)}/rt/write_bulk`, { id, values }));
export const rtReadBulkById = (name, id) =>
  unwrap(api.post(`${projectPath(name)}/rt/read_bulk`, { id }));
export const listProjectFiles = (name, ext = null) =>
  unwrap(api.get(`${projectPath(name)}/files`, { params: ext ? { ext } : {} }));
export const uploadProjectFile = (name, file) => {
  const form = new FormData();
  form.append('file', file);
  return unwrap(api.post(`${projectPath(name)}/uploads`, form));
};

export const projectFileUrl = (name, relpath) =>
  `${api.defaults.baseURL}${projectPath(name)}/files/${relpath.split('/').map(encodeURIComponent).join('/')}`;
export const downloadUrl = (name) => `${api.defaults.baseURL}${projectPath(name)}/download`;
export const downloadGeneratedUrl = (name) =>
  `${api.defaults.baseURL}${projectPath(name)}/generated/archive`;

/** Extract a readable message from an axios error. */
export const errorDetail = (e) =>
  e?.response?.data?.detail || e?.message || String(e);
