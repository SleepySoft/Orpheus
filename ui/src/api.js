import axios from 'axios';

// Dev mode (CRA on :3000) talks to the API on :8000 via CORS; when the UI is
// hosted by the backend itself (single-command mode) use same-origin /api.
const api = axios.create({
  baseURL:
    process.env.REACT_APP_ORPHEUS_API ||
    (window.location.port === '3000' ? 'http://localhost:8000/api' : '/api'),
});

const unwrap = (p) => p.then((r) => r.data);

// 冒号在工程目录名中非法，可无歧义地将嵌套路径编码为一个 URL 段。
export const projectPath = (name) => `/projects/${encodeURIComponent(name.replace(/\//g, ':'))}`;

export const listComponents = () => unwrap(api.get('/components'));
export const listProjectComponents = (name) => unwrap(api.get(`${projectPath(name)}/components`));
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
export const listProjects = () => unwrap(api.get('/projects'));
export const listExamples = () => unwrap(api.get('/examples'));
export const createProject = (name, fromExample = null) =>
  unwrap(api.post('/projects', { name, from_example: fromExample }));
export const importDistilled = (name, yamlText) =>
  unwrap(api.post(`${projectPath(name)}/distill`, { yaml: yamlText }));
export const getProject = (name) => unwrap(api.get(projectPath(name)));
export const saveProject = (name, doc) => unwrap(api.put(projectPath(name), doc));
export const checkLesson = (name) => unwrap(api.post(`${projectPath(name)}/lesson/check`));
export const getProjectNotes = (name) => unwrap(api.get(`${projectPath(name)}/notes`));
export const saveProjectNotes = (name, content) =>
  unwrap(api.put(`${projectPath(name)}/notes`, { content }));
export const getNodeNotes = (name) => unwrap(api.get(`${projectPath(name)}/node_notes`));
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
