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
  onParamChange,
  onNotesChange,
  onDeleteNode,
  onRenameNode,
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

  const { component, params, parameters, notes } = node.data;

  // Subcomponent instance: no promoted parameters in v1; edit by opening it.
  if (component?.startsWith('sub:')) {
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
          <span className="muted">子组件 {component}</span>
        </p>
        <p className="muted">子组件实例没有可提升参数（v1）。双击节点打开子组件，在独立视图中编辑内部图。</p>
        <details className="node-notes-section">
          <summary>节点注释 {node.data.notes ? '(已填写)' : ''}</summary>
          <textarea
            className="node-notes-textarea"
            placeholder="记录该子组件实例在此工程中的作用…"
            value={node.data.notes || ''}
            onChange={(e) => onNotesChange(node.id, e.target.value)}
            rows={5}
          />
        </details>
        <button className="danger" onClick={() => onDeleteNode(node.id)}>
          删除节点
        </button>
      </div>
    );
  }

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
      {universal.length > 0 && (
        <>
          <div className="param-section">通用</div>
          {universal.map(renderField)}
          <hr className="param-divider" />
        </>
      )}
      {specific.length > 0 && universal.length > 0 && <div className="param-section">组件参数</div>}
      {specific.map(renderField)}
      <details className="node-notes-section">
        <summary>节点注释 {notes ? '(已填写)' : ''}</summary>
        <textarea
          className="node-notes-textarea"
          placeholder="记录该节点在此工程中的作用、参数讲究等…"
          value={notes || ''}
          onChange={(e) => onNotesChange(node.id, e.target.value)}
          rows={5}
        />
      </details>
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
