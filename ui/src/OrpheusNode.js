import React from 'react';
import { createPortal } from 'react-dom';
import { Handle, Position } from 'reactflow';
import { resolveExprValue } from './graphUtils';
import { NODE_WIDGETS } from './nodeWidgets';

/** Custom React Flow node: ports come from the component manifest (resolved). */
export default function OrpheusNode({ data, selected }) {
  const [enlarged, setEnlarged] = React.useState(false);
  const ports = data.ports || [];
  const inputs = ports.filter((p) => p.direction === 'input');
  const outputs = ports.filter((p) => p.direction === 'output');
  const isSub = (data.component || '').startsWith('sub:');
  const shortName = isSub ? '📦 子组件（双击打开）' : (data.component || '').split('.').pop();

  // Each handle is anchored to its own port row (position: relative in CSS),
  // so pins never overlap and each wire lands on a labeled, distinct pin.
  const handleStyle = { top: '50%', transform: 'translateY(-50%)' };

  // resolved channel count badge, e.g. "out0 · 1ch"
  const channelBadge = (p) => {
    const ch = resolveExprValue(p.channels, data.params, { parameters: data.parameters });
    return typeof ch === 'number' && ch > 0 ? <span className="ch-badge">{ch}ch</span> : null;
  };

  const BodyWidget = NODE_WIDGETS[data.component];

  // compiled rate badge, e.g. "48kHz" or "24kHz ÷2" (visible time tree)
  const rateBadge = (() => {
    const r = data.rate;
    if (!r || !r.sample_rate) return null;
    const khz = r.sample_rate % 1000 === 0 ? `${r.sample_rate / 1000}k` : `${(r.sample_rate / 1000).toFixed(1)}k`;
    return (
      <span className="rate-badge" title={`采样率 ${r.sample_rate} Hz，块量子 ${r.frames} 帧，分频比 ${r.divisor}`}>
        {khz}Hz{r.divisor > 1 ? ` ÷${r.divisor}` : ''}
      </span>
    );
  })();

  const renderRow = (p, isInput) => (
    <div key={p.id} className="port-row">
      {isInput && (
        <Handle type="target" position={Position.Left} id={p.id} style={{ ...handleStyle, left: -11 }} />
      )}
      <span>{p.id}</span>
      {channelBadge(p)}
      {!isInput && (
        <Handle type="source" position={Position.Right} id={p.id} style={{ ...handleStyle, right: -11 }} />
      )}
    </div>
  );

  return (
    <div className={`orpheus-node ${selected ? 'selected' : ''} ${isSub ? 'sub' : ''}`}>
      <div className="node-header">
        <div className="node-title">
          {data.label}
          {rateBadge}
        </div>
        <div className="node-subtitle">{shortName}</div>
        {BodyWidget && (
          <button
            className="monitor-enlarge"
            title="放大监控界面"
            onClick={(e) => {
              e.stopPropagation();
              setEnlarged(true);
            }}
          >
            ⤢
          </button>
        )}
      </div>
      <div className="node-body">
        <div className="node-ports inputs">{inputs.map((p) => renderRow(p, true))}</div>
        <div className="node-ports outputs">{outputs.map((p) => renderRow(p, false))}</div>
      </div>
      {BodyWidget && <BodyWidget data={data} />}
      {enlarged &&
        createPortal(
        <div className="monitor-overlay" onClick={() => setEnlarged(false)}>
          <div className="monitor-panel" onClick={(e) => e.stopPropagation()}>
            <div className="monitor-title">
              <span>
                {data.label} <span className="muted">({shortName})</span>
              </span>
              <button className="danger" onClick={() => setEnlarged(false)}>
                × 关闭
              </button>
            </div>
            <BodyWidget data={data} large />
          </div>
        </div>,
        document.body
        )}
    </div>
  );
}
