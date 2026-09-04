import React from 'react';
import { BaseEdge, getBezierPath } from 'reactflow';

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
}) {
  const [edgePath] = getBezierPath({
    sourceX,
    sourceY,
    sourcePosition,
    targetX,
    targetY,
    targetPosition,
  });
  return (
    <g className="audio-edge-path">
      <BaseEdge
        id={id}
        path={edgePath}
        style={{ strokeWidth: selected ? 3 : 2.2 }}
      />
    </g>
  );
}
