import React from 'react';
import ParamField from './widgets';

// Universal params (channel count, sample rate, ...) always go on top,
// separated from component-specific params.
const UNIVERSAL_IDS = new Set(['channels', 'sample_rate']);
const isUniversalParam = (p) => p.affects_signature || UNIVERSAL_IDS.has(p.id);
// readback-only, non-persistent params are probe outputs (rms/peak/waveform),
// not editable inputs. Persistent readbacks (e.g. file_path) stay editable.
const isDisplayOnly = (p) => Boolean(p.readback) && !p.affects_signature && !p.persistent;

/** Right-hand panel: edit the selected node's parameters per its manifest schema. */
export default function ParamPanel({
  node,
  viewKey,
  onParamChange,
  onNodeNoteChange,
  nodeNotes,
  onDeleteNode,
  onRenameNode,
  tasks,
  onTaskChange,
  ctx,
}) {
  if (!node) {
    return (
      <div className="sidebar">
        <h3>参数面板</h3>
        <p className="muted">选择一个节点编辑参数；从左侧拖入组件添加节点。</p>
      </div>
    );
  }

  const { component, params, parameters } = node.data;
  const noteKey = viewKey === 'main' ? node.id : `${viewKey}/${node.id}`;
  const nodeNote = nodeNotes?.[noteKey] || '';

  const schemaIds = new Set((parameters || []).map((p) => p.id));
  const extraKeys = Object.keys(params || {}).filter((k) => !schemaIds.has(k));
  const editable = (parameters || []).filter((p) => !isDisplayOnly(p));
  const universal = editable.filter(isUniversalParam);
  const specific = editable.filter((p) => !isUniversalParam(p));

  const renderField = (schema) => (
    <ParamField
      key={schema.id}
      schema={schema}
      value={params?.[schema.id] ?? schema.default}
      onChange={onParamChange}
      ctx={ctx}
    />
  );

  return (
    <div className="sidebar">
      <h3>参数面板</h3>
      <p className="node-ref">
        <span className="muted">{node.id}</span>
        <br />
        <strong>{node.data.label || node.id}</strong>
        <button className="rename-btn" onClick={() => onRenameNode(node.id)} title="重命名节点（显示名）">
          重命名
        </button>
        <br />
        <span className="muted">{component}</span>
      </p>
      <div className="param-field">
        <label>所属 Task</label>
        <select value={node.data.task || 'default'} onChange={(e) => onTaskChange(node.id, e.target.value)}>
          {(tasks || []).map((task) => <option key={task.id} value={task.id}>{task.name || task.id}</option>)}
        </select>
      </div>
      {universal.length > 0 && (
        <>
          <div className="param-section">通用</div>
          {universal.map(renderField)}
          <hr className="param-divider" />
        </>
      )}
      {specific.length > 0 && universal.length > 0 && <div className="param-section">组件参数</div>}
      {specific.map(renderField)}
      <NodeNoteSection
        viewKey={viewKey}
        nodeId={node.id}
        note={nodeNote}
        onChange={onNodeNoteChange}
      />
      {extraKeys.map((key) => (
        <ParamField
          key={key}
          schema={{ id: key, name: key, type: 'string' }}
          value={params[key]}
          onChange={onParamChange}
          ctx={ctx}
        />
      ))}
      <button className="danger" onClick={() => onDeleteNode(node.id)}>
        删除节点
      </button>
    </div>
  );
}

/** Editable note for a single node instance; persisted in node-notes.json, not project.yaml. */
function NodeNoteSection({ viewKey, nodeId, note, onChange }) {
  return (
    <details className="node-notes-section" open={!!note}>
      <summary>节点笔记 {note ? '(已填写)' : ''}</summary>
      <textarea
        className="node-notes-textarea"
        placeholder={`记录 ${nodeId} 在此工程中的作用、参数讲究等…`}
        value={note}
        onChange={(e) => onChange(viewKey, nodeId, e.target.value)}
        rows={4}
      />
      <p className="muted" style={{ fontSize: 11, margin: '4px 0 0' }}>
        保存在 <code>node-notes.json</code>，不写入 <code>project.yaml</code>。
      </p>
    </details>
  );
}
