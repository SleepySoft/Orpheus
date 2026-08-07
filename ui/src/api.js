import axios from 'axios';

// Dev mode (CRA on :3000) talks to the API on :8000 via CORS; when the UI is
// hosted by the backend itself (single-command mode) use same-origin /api.
const api = axios.create({
  baseURL:
    process.env.REACT_APP_ORPHEUS_API ||
    (window.location.port === '3000' ? 'http://localhost:8000/api' : '/api'),
});

const unwrap = (p) => p.then((r) => r.data);

export const listComponents = () => unwrap(api.get('/components'));
export const rescanComponents = () => unwrap(api.post('/components/rescan'));
export const listProjects = () => unwrap(api.get('/projects'));
export const listExamples = () => unwrap(api.get('/examples'));
export const createProject = (name, fromExample = null) =>
  unwrap(api.post('/projects', { name, from_example: fromExample }));
export const importDistilled = (name, yamlText) =>
  unwrap(api.post(`/projects/${name}/distill`, { yaml: yamlText }));
export const getProject = (name) => unwrap(api.get(`/projects/${name}`));
export const saveProject = (name, doc) => unwrap(api.put(`/projects/${name}`, doc));
export const deleteProject = (name) => unwrap(api.delete(`/projects/${name}`));
export const compileProject = (name) => unwrap(api.post(`/projects/${name}/compile`));
export const runProject = (name, pace) =>
  unwrap(api.post(`/projects/${name}/run`, null, { params: { pace: pace ? 1 : 0 } }));
export const runGenerated = (name) => unwrap(api.post(`/projects/${name}/run_generated`));
export const listDevices = () => unwrap(api.get('/devices'));
export const rtStart = (name) => unwrap(api.post(`/projects/${name}/rt/start`));
export const rtStop = (name) => unwrap(api.post(`/projects/${name}/rt/stop`));
export const rtStatus = (name) => unwrap(api.get(`/projects/${name}/rt/status`));
export const rtSetParam = (name, node, param, value) =>
  unwrap(api.post(`/projects/${name}/rt/param`, { node, param, value }));
export const rtWriteBulk = (name, node, key, values) =>
  unwrap(api.post(`/projects/${name}/rt/bulk`, { node, key, values }));
export const listProjectFiles = (name, ext = null) =>
  unwrap(api.get(`/projects/${name}/files`, { params: ext ? { ext } : {} }));
export const uploadProjectFile = (name, file) => {
  const form = new FormData();
  form.append('file', file);
  return unwrap(api.post(`/projects/${name}/uploads`, form));
};

export const projectFileUrl = (name, relpath) =>
  `${api.defaults.baseURL}/projects/${name}/files/${relpath}`;
export const downloadUrl = (name) => `${api.defaults.baseURL}/projects/${name}/download`;

/** Extract a readable message from an axios error. */
export const errorDetail = (e) =>
  e?.response?.data?.detail || e?.message || String(e);
