import React from 'react';
import { createPortal } from 'react-dom';
import { Handle, Position, NodeResizer } from 'reactflow';
import { resolveExprValue } from './graphUtils';
import { NODE_WIDGETS } from './nodeWidgets';
import { NodeActionsContext } from './NodeActionsContext';

/** Custom React Flow node: ports come from the component manifest (resolved). */
export default function OrpheusNode({ data, selected }) {
  const [enlarged, setEnlarged] = React.useState(false);
  const ports = data.ports || [];
  const inputs = ports.filter((p) => p.direction === 'input');
  const outputs = ports.filter((p) => p.direction === 'output');
  const isSub = (data.component || '').startsWith('sub:');
  const shortName = data.missing
    ? '未映射组件'
    : isSub
      ? '📦 子组件（双击打开）'
      : (data.component || '').split('.').pop();

  // Each handle is anchored to its own port row (position: relative in CSS),
  // so pins never overlap and each wire lands on a labeled, distinct pin.
  const handleStyle = { top: '50%', transform: 'translateY(-50%)' };

  // resolved channel count badge, e.g. "out0 · 1ch"
  const channelBadge = (p) => {
    const ch = resolveExprValue(p.channels, data.params, { parameters: data.parameters });
    return typeof ch === 'number' && ch > 0 ? <span className="ch-badge">{ch}ch</span> : null;
  };


/**
 * Noise-detector node status: inspect last readback probes and map to a
 * visual alert state. Pure observation - this component never processes
 * audio itself, so "coloring" is the only UI feedback the user asked for.
 */
function noiseStatusClass(data) {
  const probe = data.probe;
  if (!probe) return '';
  const comp = data.component || '';
  if (comp === 'orpheus.builtin.noise_detector_ab') {
    const thd = probe.thd_n_db;
    const ratio = probe.noise_ratio ?? 0;
    const clicks = probe.clicks ?? 0;
    let level = 0;
    if (clicks > 0) level = Math.max(level, 2);
    if (ratio > 0.15) level = Math.max(level, 2);
    else if (ratio > 0.03) level = Math.max(level, 1);
    if (typeof thd === 'number') {
      if (thd > -30) level = Math.max(level, 2);
      else if (thd > -50) level = Math.max(level, 1);
    }
    return level >= 2 ? 'node-alert-danger' : level === 1 ? 'node-alert-warn' : '';
  }
  if (comp === 'orpheus.builtin.noise_detector') {
    const flat = probe.flatness ?? 0;
    const clicks = probe.clicks ?? 0;
    let level = 0;
    if (clicks > 0) level = Math.max(level, 2);
    if (flat > 0.55) level = Math.max(level, 2);
    else if (flat > 0.3) level = Math.max(level, 1);
    return level >= 2 ? 'node-alert-danger' : level === 1 ? 'node-alert-warn' : '';
  }
  return '';
}

  const BodyWidget = NODE_WIDGETS[data.component];
  const { showReadme } = React.useContext(NodeActionsContext);

  // compiled rate badge, e.g. "48kHz" or "24kHz ÷2" (visible time tree)
  // 时钟源（信号/扫频/设备/wav 输入）显示 ⏱ 徽标：图采样率以它为准
  const clockSource = !!data.clockSource;
  const rateBadge = (() => {
    const r = data.rate;
    if (!r || !r.sample_rate) {
      return clockSource ? (
        <span className="clock-badge" title="时钟源（图采样率以它为准）">⏱ 时钟源</span>
      ) : null;
    }
    const khz = r.sample_rate % 1000 === 0 ? `${r.sample_rate / 1000}k` : `${(r.sample_rate / 1000).toFixed(1)}k`;
    const txt = `${khz}Hz${r.divisor > 1 ? ` ÷${r.divisor}` : ''}`;
    if (clockSource) {
      return (
        <span className="clock-badge" title={`时钟源：图采样率 ${r.sample_rate} Hz（以它为准），分频比 ${r.divisor}`}>
          ⏱ {txt}
        </span>
      );
    }
    return (
      <span className="rate-badge" title={`采样率 ${r.sample_rate} Hz，块量子 ${r.frames} 帧，分频比 ${r.divisor}`}>
        {txt}
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
    <>
      {selected && (
        <NodeResizer
          isVisible={selected}
          minWidth={160}
          minHeight={80}
          color="#4cc9f0"
        />
      )}
      <div className={`orpheus-node ${selected ? 'selected' : ''} ${isSub ? 'sub' : ''} ${data.missing ? 'missing' : ''} ${noiseStatusClass(data)}`}>
      <div className="node-header">
        <div className="node-title">
          {data.label}
          {data.missing && (
            <span className="missing-badge" title="该组件已被删除：请删除此节点或重新创建组件">
              组件缺失
            </span>
          )}
          {rateBadge}
          {data.hasNote && (
            <span className="note-badge" title="该节点有笔记">
              📝
            </span>
          )}
        </div>
        <div className="node-subtitle">{shortName}</div>
        {noiseStatusClass(data) && (
          <span className="node-alert-dot" title="??????" />
        )}
        {data.missing && data.params && data.params.note ? (
          <div className="node-note" title={data.params.note}>
            {data.params.note}
          </div>
        ) : null}
        <div className="node-actions">
          {!data.missing && !isSub && (
            <button
              className="node-info-btn"
              title="查看组件说明"
              onClick={(e) => {
                e.stopPropagation();
                showReadme(data.component);
              }}
            >
              ℹ
            </button>
          )}
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
    </>
  );
}
