import React from 'react';

/**
 * Node body widget registry: customize what a node shows on the canvas,
 * keyed by component id. Receives node data (incl. data.probe readback
 * values from the last run). Register here to give a component on-node
 * display/interaction without touching the framework.
 */

function LevelBar({ label, value }) {
  // map linear 0..1 to a bar; clamp display
  const pct = Math.max(0, Math.min(1, value ?? 0)) * 100;
  const db = value > 0 ? (20 * Math.log10(value)).toFixed(1) : '-∞';
  return (
    <div className="level-row">
      <span className="level-label">{label}</span>
      <div className="level-bar">
        <div className="level-fill" style={{ width: `${pct}%` }} />
      </div>
      <span className="level-value">{db} dB</span>
    </div>
  );
}

function ProbeRmsWidget({ data }) {
  const v = data.probe?.rms;
  return (
    <div className="probe-body">
      {v !== undefined ? <LevelBar label="RMS" value={v} /> : <span className="muted">运行后显示电平</span>}
    </div>
  );
}

function ProbePeakWidget({ data }) {
  const v = data.probe?.peak;
  return (
    <div className="probe-body">
      {v !== undefined ? <LevelBar label="Peak" value={v} /> : <span className="muted">运行后显示峰值</span>}
    </div>
  );
}

export const NODE_WIDGETS = {
  'orpheus.builtin.probe_rms': ProbeRmsWidget,
  'orpheus.builtin.probe_peak': ProbePeakWidget,
};
