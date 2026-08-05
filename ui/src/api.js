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
export const getProject = (name) => unwrap(api.get(`/projects/${name}`));
export const saveProject = (name, doc) => unwrap(api.put(`/projects/${name}`, doc));
export const deleteProject = (name) => unwrap(api.delete(`/projects/${name}`));
export const compileProject = (name) => unwrap(api.post(`/projects/${name}/compile`));
export const runProject = (name) => unwrap(api.post(`/projects/${name}/run`));

export const projectFileUrl = (name, relpath) =>
  `${api.defaults.baseURL}/projects/${name}/files/${relpath}`;
export const downloadUrl = (name) => `${api.defaults.baseURL}/projects/${name}/download`;

/** Extract a readable message from an axios error. */
export const errorDetail = (e) =>
  e?.response?.data?.detail || e?.message || String(e);
