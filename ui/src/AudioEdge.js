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
  const [defaultPath, defaultLabelX, defaultLabelY] = getBezierPath({
    sourceX,
    sourceY,
    sourcePosition,
    targetX,
    targetY,
    targetPosition,
  });
  const isProjection = !!data?.projectionKind;
  const direction = targetX >= sourceX ? 1 : -1;
  const controlDistance = Math.max(70, Math.abs(targetX - sourceX) * 0.45);
  const offset = data?.curveOffset || 0;
  const projectionPath = [
    `M ${sourceX},${sourceY}`,
    `C ${sourceX + direction * controlDistance},${sourceY + offset}`,
    `${targetX - direction * controlDistance},${targetY + offset}`,
    `${targetX},${targetY}`,
  ].join(' ');
  const edgePath = isProjection ? projectionPath : defaultPath;
  const labelX = isProjection ? (sourceX + targetX) / 2 : defaultLabelX;
  const labelY = isProjection ? (sourceY + targetY) / 2 + offset * 0.75 : defaultLabelY;
  const isProjectedAudio = data?.projectionKind === 'audio';
  const projectionStyle = isProjectedAudio
    ? { stroke: '#1971a8', strokeWidth: selected ? 3 : 2.4 }
    : { stroke: '#d97706', strokeWidth: selected ? 2.8 : 2, strokeDasharray: '5 4' };
  return (
    <>
      <g className={isProjection ? `projection-edge-path projection-edge-${data.projectionKind}` : 'audio-edge-path'}>
        <BaseEdge
          id={id}
          path={edgePath}
          style={isProjection ? projectionStyle : { strokeWidth: selected ? 3 : 2.2 }}
        />
      </g>
      {isProjection && (
        <EdgeLabelRenderer>
          <div
            className={`projection-edge-label projection-edge-label-${data.projectionKind}`}
            style={{ transform: `translate(-50%, -50%) translate(${labelX}px, ${labelY}px)` }}
            title={`${data.busId}\n${data.words} words total · ${data.atomicity || 'unverified'}\n${(data.fields || []).map((field) => `${field.id} [${field.shape.join('×')}] = ${field.words} words @ ${field.offset_words}`).join('\n')}`}
          >
            {data.label}
          </div>
        </EdgeLabelRenderer>
      )}
    </>
  );
}
