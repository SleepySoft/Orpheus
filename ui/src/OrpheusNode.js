import React from 'react';
import { Handle, Position } from 'reactflow';

/** Custom React Flow node: ports come from the component manifest. */
export default function OrpheusNode({ data, selected }) {
  const ports = data.ports || [];
  const inputs = ports.filter((p) => p.direction === 'input');
  const outputs = ports.filter((p) => p.direction === 'output');
  const isSub = (data.component || '').startsWith('sub:');
  const shortName = isSub ? '📦 子组件（双击打开）' : (data.component || '').split('.').pop();

  // Each handle is anchored to its own port row (position: relative in CSS),
  // so pins never overlap and each wire lands on a labeled, distinct pin.
  const handleStyle = { top: '50%', transform: 'translateY(-50%)' };

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
              <Handle
                type="target"
                position={Position.Left}
                id={p.id}
                style={{ ...handleStyle, left: -11 }}
              />
              <span>{p.id}</span>
            </div>
          ))}
        </div>
        <div className="node-ports outputs">
          {outputs.map((p) => (
            <div key={p.id} className="port-row">
              <span>{p.id}</span>
              <Handle
                type="source"
                position={Position.Right}
                id={p.id}
                style={{ ...handleStyle, right: -11 }}
              />
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
