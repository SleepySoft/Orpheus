import React from 'react';
import { BaseEdge, EdgeLabelRenderer, getBezierPath } from 'reactflow';
import { shapeText } from './graphUtils';

/**
 * 控制链路自定义边：橙色虚线（流动动画）+ 边中形状标注。
 * data = { srcShape, dstShape, mismatch }：两端求值后的形状与失配标记，
 * 由 App 侧按节点当前参数派生注入；失配时线与 label 变红并加 ⚠（不剪线）。
 */
export default function ControlEdge({
  id,
  sourceX,
  sourceY,
  targetX,
  targetY,
  sourcePosition,
  targetPosition,
  data,
  selected,
}) {
  const [edgePath, labelX, labelY] = getBezierPath({
    sourceX,
    sourceY,
    sourcePosition,
    targetX,
    targetY,
    targetPosition,
  });
  const mismatch = !!data?.mismatch;
  const color = mismatch ? '#e03131' : '#f0a24c';
  const label = `${shapeText(data?.srcShape)}→${shapeText(data?.dstShape)}`;
  return (
    <>
      <g className="control-edge-path">
        <BaseEdge
          id={id}
          path={edgePath}
          style={{
            stroke: color,
            strokeWidth: selected ? 2.5 : 1.8,
            strokeDasharray: '6 4',
          }}
        />
      </g>
      <EdgeLabelRenderer>
        <div
          className={`control-edge-label ${mismatch ? 'mismatch' : ''}`}
          style={{ transform: `translate(-50%, -50%) translate(${labelX}px, ${labelY}px)` }}
          title={
            mismatch
              ? '两端形状不匹配：修改相关参数恢复匹配后自动复原（连线保留不剪）'
              : '控制链路：控制源参数 → 可绑定参数（块边界投递）'
          }
        >
          {mismatch ? `⚠ ${label}` : label}
        </div>
      </EdgeLabelRenderer>
    </>
  );
}
