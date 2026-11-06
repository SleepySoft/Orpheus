import React from 'react';
import { BaseEdge, EdgeLabelRenderer, getBezierPath } from 'reactflow';
import { driveTimingLabel } from './graphUtils';

/** 节点级驱动箭头，不占用普通数据端口。 */
export default function DriveEdge({
  id, sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition, selected, data,
}) {
  const markerId = `drive-arrow-${React.useId().replace(/:/g, '')}`;
  const [editing, setEditing] = React.useState(false);
  const [every, setEvery] = React.useState('1');
  const [phase, setPhase] = React.useState('0');
  const [error, setError] = React.useState('');
  const [path, x, y] = getBezierPath({
    sourceX, sourceY, targetX, targetY, sourcePosition, targetPosition,
  });
  const beginEdit = (event) => {
    event.stopPropagation();
    setEvery(String(data?.every ?? 1));
    setPhase(String(data?.phase ?? ((data?.every ?? 1) - 1)));
    setError('');
    setEditing(true);
  };
  const save = (event) => {
    event.preventDefault();
    event.stopPropagation();
    const period = Number(every);
    const offset = Number(phase);
    if (!every.trim() || !phase.trim() || !Number.isSafeInteger(period) || period < 1
      || !Number.isSafeInteger(offset) || offset < 0 || offset >= period) {
      setError('周期须为正整数，相位须在0到周期−1之间');
      return;
    }
    data.onTimingChange({ every: period, phase: offset });
    setEditing(false);
  };
  return (
    <>
      <defs>
        <marker id={markerId} viewBox="0 0 10 10" refX="9" refY="5"
          markerWidth="7" markerHeight="7" orient="auto-start-reverse">
          <path d="M 0 0 L 10 5 L 0 10 z" fill="#b197fc" />
        </marker>
      </defs>
      <BaseEdge id={id} path={path} markerEnd={`url(#${markerId})`}
        style={{ stroke: '#b197fc', strokeWidth: selected ? 3 : 2, strokeDasharray: '10 3 2 3' }} />
      <EdgeLabelRenderer>
        <div className="drive-edge-label nodrag nopan"
          style={{ transform: `translate(-50%, -50%) translate(${x}px, ${y}px)` }}
          title="节点级驱动，不传输PCM；分频只选触发拍，不匹配Buffer">
          {data?.onTimingChange ? (
            <button type="button" className="drive-timing-button" onClick={beginEdit}>
              驱动 →{driveTimingLabel(data) ? ` ${driveTimingLabel(data)}` : ''}
            </button>
          ) : <>驱动 →{driveTimingLabel(data) ? ` ${driveTimingLabel(data)}` : ''}</>}
          {editing && (
            <form className="drive-timing-editor" onSubmit={save} onClick={(event) => event.stopPropagation()}>
              <strong>分频 / 选拍</strong>
              <label>每几拍选1拍
                <input aria-label="驱动周期" type="number" min="1" step="1"
                  value={every} onChange={(event) => setEvery(event.target.value)} />
              </label>
              <label>相位（从0计数）
                <input aria-label="驱动相位" type="number" min="0" step="1"
                  value={phase} onChange={(event) => setPhase(event.target.value)} />
              </label>
              <small>只选择触发拍，不改变PCM；Buffer由用户匹配。</small>
              {error && <span role="alert">{error}</span>}
              <div>
                <button type="submit">保存</button>
                <button type="button" onClick={() => setEditing(false)}>取消</button>
              </div>
            </form>
          )}
        </div>
      </EdgeLabelRenderer>
    </>
  );
}
