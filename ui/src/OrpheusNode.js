import React from 'react';
import { Handle, Position } from 'reactflow';

/** Custom React Flow node: ports come from the component manifest. */
export default function OrpheusNode({ data, selected }) {
  const ports = data.ports || [];
  const inputs = ports.filter((p) => p.direction === 'input');
  const outputs = ports.filter((p) => p.direction === 'output');
  const isSub = (data.component || '').startsWith('sub:');
  const shortName = isSub ? '📦 子组件（双击打开）' : (data.component || '').split('.').pop();

  return (
    <div className={`orpheus-node ${selected ? 'selected' : ''} ${isSub ? 'sub' : ''}`}>
      <div className="node-header">
        <div className="node-title">{data.label}</div>
        <div className="node-subtitle">{shortName}</div>
      </div>
      <div className="node-body">
        <div className="node-ports inputs">
          {inputs.map((p) => (
            <div key={p.id} className="port-row">
              <Handle type="target" position={Position.Left} id={p.id} />
              <span>{p.id}</span>
            </div>
          ))}
        </div>
        <div className="node-ports outputs">
          {outputs.map((p) => (
            <div key={p.id} className="port-row">
              <span>{p.id}</span>
              <Handle type="source" position={Position.Right} id={p.id} />
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
