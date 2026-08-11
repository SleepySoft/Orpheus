import React, { useEffect, useState } from 'react';
import ReactMarkdown from 'react-markdown';
import * as api from './api';

/** Left-panel project notes editor.
 *
 * Notes are stored in a sidecar file `workspace/<project>/notes.md`,
 * completely separate from `project.yaml`. The file is free-form Markdown;
 * a recommended convention is to use `## 节点: <node_id>` sections for
 * per-node commentary.
 */
export default function NotesPanel({ projectName }) {
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
            `# 工程笔记\n\n在这里记录整个工程的设计思路、调参记录、读图学习笔记等。\n\n## 节点: <node_id>\n示例：说明某个节点为什么放在这里、参数有什么讲究。\n`
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

  return (
    <div className="sidebar notes-panel">
      <h3>工程笔记</h3>
      <p className="muted">
        保存在 <code>workspace/{projectName}/notes.md</code>，不混入 <code>project.yaml</code>。
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
          <ReactMarkdown>{content}</ReactMarkdown>
        </div>
      )}
    </div>
  );
}
