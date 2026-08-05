import React, { useEffect, useState } from 'react';
import * as api from './api';

/** Modal to pick or upload a file inside the current project directory. */
export default function FileBrowseModal({ projectName, ext, onSelect, onClose }) {
  const [files, setFiles] = useState(null);
  const [error, setError] = useState(null);

  const refresh = () => {
    api
      .listProjectFiles(projectName, ext)
      .then(setFiles)
      .catch((e) => setError(api.errorDetail(e)));
  };

  useEffect(refresh, [projectName, ext]);

  const onUpload = async (e) => {
    const file = e.target.files?.[0];
    if (!file) return;
    try {
      const r = await api.uploadProjectFile(projectName, file);
      onSelect(r.path);
    } catch (err) {
      setError(api.errorDetail(err));
    }
  };

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal" onClick={(e) => e.stopPropagation()}>
        <h4>选择工程内文件{ext ? `（${ext}）` : ''}</h4>
        {error && <p className="modal-error">{error}</p>}
        <div className="modal-file-list">
          {files === null && <p className="muted">加载中…</p>}
          {files && files.length === 0 && <p className="muted">工程目录暂无匹配文件，请上传</p>}
          {(files || []).map((f) => (
            <div key={f.path} className="modal-file-row" onClick={() => onSelect(f.path)}>
              <span>{f.path}</span>
              <span className="muted">{(f.size / 1024).toFixed(1)} KB</span>
            </div>
          ))}
        </div>
        <div className="modal-actions">
          <label className="upload-btn">
            上传文件…
            <input type="file" accept={ext || undefined} style={{ display: 'none' }} onChange={onUpload} />
          </label>
          <button onClick={onClose}>取消</button>
        </div>
      </div>
    </div>
  );
}
