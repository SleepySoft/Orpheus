import React from 'react';
import { BaseEdge, EdgeLabelRenderer } from 'reactflow';

/** Read-only deployment/message relation derived from Algorithm IR buses. */
export default function MessageEdge({
  id,
  sourceX,
  sourceY,
  targetX,
  targetY,
  data,
  selected,
}) {
  const direction = targetX >= sourceX ? 1 : -1;
  const controlDistance = Math.max(70, Math.abs(targetX - sourceX) * 0.45);
  const offset = data?.curveOffset || 0;
  const path = [
    `M ${sourceX},${sourceY}`,
    `C ${sourceX + direction * controlDistance},${sourceY + offset}`,
    `${targetX - direction * controlDistance},${targetY + offset}`,
    `${targetX},${targetY}`,
  ].join(' ');
  const labelX = (sourceX + targetX) / 2;
  const labelY = (sourceY + targetY) / 2 + offset * 0.75;
  const fields = (data?.fields || []).map((field) => field.id).join(', ');
  const title = `${data?.busId || 'typed bus'}\n${data?.atomicity || 'unverified'}\n${fields}`;

  return (
    <>
      <BaseEdge
        id={id}
        path={path}
        style={{
          stroke: '#147d72',
          strokeWidth: selected ? 2.8 : 2,
          strokeDasharray: '3 5',
        }}
      />
      <EdgeLabelRenderer>
        <div
          className="message-edge-label"
          style={{ transform: `translate(-50%, -50%) translate(${labelX}px, ${labelY}px)` }}
          title={title}
        >
          {data?.busId} · {data?.label}
        </div>
      </EdgeLabelRenderer>
    </>
  );
}
