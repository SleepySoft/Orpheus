import React, { useMemo } from 'react';
import { useViewport } from 'reactflow';
import { alterGroupScreenBounds, alterGroups } from './graphUtils';

export default function AlterGroupOverlay({ nodes }) {
  const viewport = useViewport();
  const groups = useMemo(
    () => alterGroups(nodes).map((members) => ({
      members,
      bounds: alterGroupScreenBounds(nodes, members, viewport),
    })).filter((group) => group.bounds),
    [nodes, viewport]
  );

  if (!groups.length) return null;

  return (
    <div className="alter-group-overlay" aria-hidden="true">
      {groups.map(({ members, bounds }) => (
        <div
          key={members.join('|')}
          className="alter-group-frame"
          style={bounds}
          title={`相互替代：${members.join(' / ')}`}
        >
          <span>平台替代 · {members.length} 选 1</span>
        </div>
      ))}
    </div>
  );
}
