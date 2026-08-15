import React, { useEffect, useState } from 'react';
import ReactMarkdown from 'react-markdown';
import remarkGfm from 'remark-gfm';
import * as api from './api';

/** Left-panel project notes editor.
 *
 * Stores two pieces of sidecar data alongside project.yaml:
 * 1. `workspace/<project>/notes.md`      — free-form project-level notes/design log.
 * 2. `workspace/<project>/node-notes.json` — per-node instance notes (auto-sliced).
 *
 * Both are independent from project.yaml, keeping the project document clean.
 */
export default function NotesPanel({ projectName, views, nodeNotes, onNodeNoteChange }) {
  const [content, setContent] = useState('');
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [dirty, setDirty] = useState(false);
  const [mode, setMode] = useState('edit'); // 'edit' | 'preview'
  const [status, setStatus] = useState('');

  useEffect(() => {
    if (!projectName) {
      setLoading(false);
      setContent('');
      return undefined;
    }
    let cancelled = false;
    setLoading(true);
    setStatus('');
    api
      .getProjectNotes(projectName)
      .then((r) => {
        if (cancelled) return;
        setContent(r.content || '');
        setLoading(false);
      })
      .catch((e) => {
        if (cancelled) return;
        if (e?.response?.status === 404) {
          setContent(
            `# 工程笔记\n\n在这里记录整个工程的设计思路、调参记录、读图学习笔记等。\n\n左侧“参数面板”里也可以为单个节点写笔记，它们会被自动汇总到 <code>node-notes.json</code>。\n`
          );
        } else {
          setContent('');
          setStatus(`读取失败: ${api.errorDetail(e)}`);
        }
        setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [projectName]);

  const doSave = async () => {
    if (!projectName) return;
    setSaving(true);
    setStatus('');
    try {
      await api.saveProjectNotes(projectName, content);
      setDirty(false);
      setStatus('已保存');
    } catch (e) {
      setStatus(`保存失败: ${api.errorDetail(e)}`);
    } finally {
      setSaving(false);
    }
  };

  // Autosave 1.5s after the last keystroke.
  useEffect(() => {
    if (!dirty || !projectName) return undefined;
    const timer = setTimeout(doSave, 1500);
    return () => clearTimeout(timer);
  }, [dirty, content, projectName]);

  if (loading) {
    return (
      <div className="sidebar notes-panel">
        <h3>工程笔记</h3>
        <p className="muted">加载中…</p>
      </div>
    );
  }

  if (!projectName) {
    return (
      <div className="sidebar notes-panel">
        <h3>工程笔记</h3>
        <p className="muted">先打开或创建一个工程。</p>
      </div>
    );
  }

  // Build a stable (viewKey, nodeId) -> note key map, matching App.js logic.
  const allNodes = [];
  for (const [viewKey, v] of Object.entries(views || {})) {
    for (const n of v.nodes || []) {
      const key = viewKey === 'main' ? n.id : `${viewKey}/${n.id}`;
      allNodes.push({ viewKey, nodeId: n.id, label: n.data?.label || n.id, key });
    }
  }
  const nodeEntries = allNodes
    .filter((n) => nodeNotes?.[n.key])
    .sort((a, b) => a.key.localeCompare(b.key));

  return (
    <div className="sidebar notes-panel">
      <h3>工程笔记</h3>
      <p className="muted">
        保存在 <code>workspace/{projectName}/notes.md</code> 与 <code>node-notes.json</code>，不混入 <code>project.yaml</code>。
      </p>
      <div className="notes-toolbar">
        <button
          className={mode === 'edit' ? 'active' : ''}
          onClick={() => setMode('edit')}
          title="编辑 Markdown 源码"
        >
          编辑
        </button>
        <button
          className={mode === 'preview' ? 'active' : ''}
          onClick={() => setMode('preview')}
          title="预览渲染效果"
        >
          预览
        </button>
        <button onClick={doSave} disabled={saving} title="Ctrl+S 也可保存">
          {saving ? '保存中…' : '保存'}
        </button>
      </div>
      {status && <div className="notes-status">{status}</div>}
      {mode === 'edit' ? (
        <textarea
          className="notes-editor"
          value={content}
          onChange={(e) => {
            setContent(e.target.value);
            setDirty(true);
            setStatus('');
          }}
          spellCheck={false}
          placeholder="# 工程笔记\n\n在这里写设计思路、节点说明、调参记录…"
        />
      ) : (
        <div className="notes-preview readme-content">
          <ReactMarkdown remarkPlugins={[remarkGfm]}>{content}</ReactMarkdown>
        </div>
      )}

      <details className="node-notes-rollup">
        <summary>节点笔记汇总（{nodeEntries.length} 条）</summary>
        <div className="node-notes-list">
          {nodeEntries.length === 0 && (
            <p className="muted">暂无节点笔记。选中节点，在参数面板里添加。</p>
          )}
          {nodeEntries.map((n) => (
            <div key={n.key} className="node-note-item">
              <div className="node-note-item-title" title={n.key}>
                {n.viewKey === 'main' ? '' : `${n.viewKey}/`}
                {n.label}
              </div>
              <textarea
                className="node-notes-textarea"
                rows={3}
                value={nodeNotes[n.key] || ''}
                onChange={(e) => onNodeNoteChange(n.viewKey, n.nodeId, e.target.value)}
              />
            </div>
          ))}
        </div>
      </details>
    </div>
  );
}
