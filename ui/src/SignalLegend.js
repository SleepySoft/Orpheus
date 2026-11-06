import React from 'react';

export default function SignalLegend({ flowMode, showControlLinks }) {
  return (
    <div className="signal-legend" aria-label="链路图例">
      <span className="legend-audio"><i />音频</span>
      {showControlLinks && <span className="legend-control"><i />控制</span>}
      {flowMode === 'task_sequence' && <span className="legend-drive"><i />驱动（节点级箭头）</span>}
    </div>
  );
}
