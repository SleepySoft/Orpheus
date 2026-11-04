import React from 'react';
import { BaseEdge, EdgeLabelRenderer, getBezierPath } from 'reactflow';
import { driveTimingLabel } from './graphUtils';

/** 框架执行顺序边：紫色箭头，与数据线及橙色参数控制线区分。 */
export default function DriveEdge({
  id, sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition, selected, data,
}) {
  const [path, x, y] = getBezierPath({
    sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition,
  });
  return (
    <>
      <BaseEdge id={id} path={path}
        style={{ stroke: '#b197fc', strokeWidth: selected ? 3 : 2, strokeDasharray: '10 3 2 3' }} />
      <EdgeLabelRenderer>
        <div className="drive-edge-label"
          style={{ transform: `translate(-50%, -50%) translate(${x}px, ${y}px)` }}
          title="框架执行顺序：由时钟源启动的执行 DAG，不传输数据或参数">
          执行 →{driveTimingLabel(data) ? ` ${driveTimingLabel(data)}` : ''}
        </div>
      </EdgeLabelRenderer>
    </>
  );
}
