import React from 'react';
import { BaseEdge, EdgeLabelRenderer, getBezierPath } from 'reactflow';

/** Audio signal edge: solid cool-colored path, visually distinct from control links. */
export default function AudioEdge({
  id,
  sourceX,
  sourceY,
  targetX,
  targetY,
  sourcePosition,
  targetPosition,
  selected,
  data,
}) {
  const [edgePath, labelX, labelY] = getBezierPath({
    sourceX,
    sourceY,
    sourcePosition,
    targetX,
    targetY,
    targetPosition,
  });
  const isMessage = !!data?.busId;
  return (
    <>
      <g className={isMessage ? 'message-edge-path' : 'audio-edge-path'}>
        <BaseEdge
          id={id}
          path={edgePath}
          style={isMessage ? {
            stroke: '#147d72',
            strokeWidth: selected ? 2.8 : 2,
            strokeDasharray: '3 5',
          } : { strokeWidth: selected ? 3 : 2.2 }}
        />
      </g>
      {isMessage && (
        <EdgeLabelRenderer>
          <div
            className="message-edge-label"
            style={{ transform: `translate(-50%, -50%) translate(${labelX}px, ${labelY}px)` }}
            title={`${data.busId}\n${data.atomicity || 'unverified'}\n${(data.fields || []).map((field) => field.id).join(', ')}`}
          >
            {data.busId} · {data.label}
          </div>
        </EdgeLabelRenderer>
      )}
    </>
  );
}
