import React from 'react';

function ParamField({ schema, value, onChange }) {
  const { id, name, type, range, unit } = schema;
  const label = (
    <label>
      {name || id}
      {unit ? ` (${unit})` : ''}
    </label>
  );

  if (type === 'float' || type === 'int') {
    return (
      <div className="param-field">
        {label}
        <input
          type="number"
          value={value ?? ''}
          step={type === 'int' ? 1 : 'any'}
          min={range?.[0]}
          max={range?.[1]}
          onChange={(e) => {
            const raw = e.target.value;
            if (raw === '') return onChange(id, type === 'int' ? 0 : 0.0);
            onChange(id, type === 'int' ? parseInt(raw, 10) : parseFloat(raw));
          }}
        />
      </div>
    );
  }

  return (
    <div className="param-field">
      {label}
      <input
        type="text"
        value={value ?? ''}
        onChange={(e) => onChange(id, e.target.value)}
      />
    </div>
  );
}

/** Right-hand panel: edit the selected node's parameters per its manifest schema. */
export default function ParamPanel({ node, onParamChange, onDeleteNode }) {
  if (!node) {
    return (
      <div className="sidebar">
        <h3>参数面板</h3>
        <p className="muted">选择一个节点编辑参数；从左侧拖入组件添加节点。</p>
      </div>
    );
  }

  const { component, params, parameters } = node.data;

  // Subcomponent instance: no promoted parameters in v1; edit by opening it.
  if (component?.startsWith('sub:')) {
    return (
      <div className="sidebar">
        <h3>参数面板</h3>
        <p className="node-ref">
          <strong>{node.id}</strong>
          <br />
          <span className="muted">子组件 {component}</span>
        </p>
        <p className="muted">子组件实例没有可提升参数（v1）。双击节点打开子组件，在独立视图中编辑内部图。</p>
        <button className="danger" onClick={() => onDeleteNode(node.id)}>
          删除节点
        </button>
      </div>
    );
  }

  const schemaIds = new Set((parameters || []).map((p) => p.id));
  const extraKeys = Object.keys(params || {}).filter((k) => !schemaIds.has(k));

  return (
    <div className="sidebar">
      <h3>参数面板</h3>
      <p className="node-ref">
        <strong>{node.id}</strong>
        <br />
        <span className="muted">{component}</span>
      </p>
      {(parameters || []).map((schema) => (
        <ParamField
          key={schema.id}
          schema={schema}
          value={params?.[schema.id] ?? schema.default}
          onChange={onParamChange}
        />
      ))}
      {extraKeys.map((key) => (
        <ParamField
          key={key}
          schema={{ id: key, name: key, type: 'string' }}
          value={params[key]}
          onChange={onParamChange}
        />
      ))}
      <button className="danger" onClick={() => onDeleteNode(node.id)}>
        删除节点
      </button>
    </div>
  );
}
