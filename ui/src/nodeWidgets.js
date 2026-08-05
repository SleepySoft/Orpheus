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

/**
 * Oscilloscope-style body widget: renders data.probe.waveform (float array,
 * produced by the probe_waveform component via PROBE_JSON readback).
 */
function ScopeWidget({ data }) {
  const samples = data.probe?.waveform;
  const ref = React.useRef(null);

  React.useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const W = canvas.width;
    const H = canvas.height;
    ctx.clearRect(0, 0, W, H);

    // dark scope background + 4-division grid + center line
    ctx.fillStyle = '#0d1117';
    ctx.fillRect(0, 0, W, H);
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    for (let gy = 0; gy <= 4; gy++) {
      const y = (H * gy) / 4;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(W, y);
      ctx.stroke();
    }
    const midY = H / 2;
    ctx.strokeStyle = 'rgba(255,255,255,0.22)';
    ctx.beginPath();
    ctx.moveTo(0, midY);
    ctx.lineTo(W, midY);
    ctx.stroke();

    if (!Array.isArray(samples) || samples.length === 0) {
      ctx.fillStyle = 'rgba(255,255,255,0.45)';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('运行后显示波形', W / 2, midY + 3);
      return;
    }

    // scope-style trace: per-column min/max over the sample window
    ctx.beginPath();
    for (let x = 0; x < W; x++) {
      const i0 = Math.floor((x / W) * samples.length);
      const i1 = Math.min(samples.length, Math.max(i0 + 1, Math.floor(((x + 1) / W) * samples.length)));
      let lo = Infinity;
      let hi = -Infinity;
      for (let i = i0; i < i1; i++) {
        const v = samples[i];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
      }
      if (lo === Infinity) continue;
      const yLo = midY - lo * (H * 0.42);
      const yHi = midY - hi * (H * 0.42);
      if (x === 0) ctx.moveTo(x, yLo);
      ctx.lineTo(x, yHi);
    }
    ctx.strokeStyle = '#4fc3f7';
    ctx.lineWidth = 1.2;
    ctx.stroke();
  }, [samples]);

  return (
    <div className="probe-body">
      <canvas
        ref={ref}
        width={180}
        height={64}
        style={{ width: '100%', height: 64, display: 'block', borderRadius: 4 }}
      />
    </div>
  );
}

export const NODE_WIDGETS = {
  'orpheus.builtin.probe_rms': ProbeRmsWidget,
  'orpheus.builtin.probe_peak': ProbePeakWidget,
  'orpheus.builtin.probe_waveform': ScopeWidget,
};
