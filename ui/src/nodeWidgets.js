import React from 'react';

/**
 * Node body widget registry: customize what a node shows on the canvas,
 * keyed by component id. Receives node data (incl. data.probe readback
 * values from the last run). Register here to give a component on-node
 * display/interaction without touching the framework.
 */

function LevelBar({ label, value, large }) {
  // map linear 0..1 to a bar; clamp display
  const pct = Math.max(0, Math.min(1, value ?? 0)) * 100;
  const db = value > 0 ? (20 * Math.log10(value)).toFixed(1) : '-∞';
  return (
    <div className={`level-row ${large ? 'large' : ''}`}>
      <span className="level-label">{label}</span>
      <div className="level-bar" style={large ? { height: 26 } : undefined}>
        <div className="level-fill" style={{ width: `${pct}%` }} />
      </div>
      <span className="level-value">{db} dB</span>
    </div>
  );
}

function ProbeRmsWidget({ data, large }) {
  const v = data.probe?.rms;
  return (
    <div className="probe-body">
      {v !== undefined ? (
        <LevelBar label="RMS" value={v} large={large} />
      ) : (
        <span className="muted">运行后显示电平</span>
      )}
    </div>
  );
}

function ProbePeakWidget({ data, large }) {
  const v = data.probe?.peak;
  return (
    <div className="probe-body">
      {v !== undefined ? (
        <LevelBar label="Peak" value={v} large={large} />
      ) : (
        <span className="muted">运行后显示峰值</span>
      )}
    </div>
  );
}

/**
 * Oscilloscope-style body widget: renders data.probe.waveform (float array,
 * produced by the probe_waveform component via PROBE_JSON readback).
 * Keeps a rolling client-side history so consecutive snapshots scroll like a
 * DAW waveform instead of jumping to a fresh window each poll.
 */
function ScopeWidget({ data, large }) {
  const samples = data.probe?.waveform;
  const ref = React.useRef(null);
  const histRef = React.useRef([]);
  const HISTORY_CAP = 8192; // ~85ms @48kHz; larger = smoother scroll, smaller = faster response
  const W = large ? 640 : 180;
  const H = large ? 240 : 64;

  React.useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const cw = canvas.width;
    const ch = canvas.height;
    ctx.clearRect(0, 0, cw, ch);

    // dark scope background + 4-division grid + center line
    ctx.fillStyle = '#0d1117';
    ctx.fillRect(0, 0, cw, ch);
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    for (let gy = 0; gy <= 4; gy++) {
      const y = (ch * gy) / 4;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(cw, y);
      ctx.stroke();
    }
    const midY = ch / 2;
    ctx.strokeStyle = 'rgba(255,255,255,0.22)';
    ctx.beginPath();
    ctx.moveTo(0, midY);
    ctx.lineTo(cw, midY);
    ctx.stroke();

    if (large) {
      // amplitude labels on the left edge
      ctx.fillStyle = 'rgba(255,255,255,0.45)';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'left';
      for (let i = 0; i <= 4; i++) {
        const amp = 1 - i / 2; // 1, 0.5, 0, -0.5, -1
        const y = (ch * i) / 4;
        ctx.fillText(amp.toFixed(1), 4, y - 3);
      }
    }

    if (!Array.isArray(samples) || samples.length === 0) {
      histRef.current = []; // new run (or stopped): start with a clean scroll
      ctx.fillStyle = 'rgba(255,255,255,0.45)';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('运行后显示波形', cw / 2, midY + 3);
      return;
    }

    // rolling history: new snapshot appended at the right, old samples scroll left
    let hist = histRef.current.concat(samples);
    if (hist.length > HISTORY_CAP) hist = hist.slice(hist.length - HISTORY_CAP);
    histRef.current = hist;

    // scope-style trace: per-column min/max envelope over the visible history
    ctx.beginPath();
    for (let x = 0; x < cw; x++) {
      const i0 = Math.floor((x / cw) * hist.length);
      const i1 = Math.min(hist.length, Math.max(i0 + 1, Math.floor(((x + 1) / cw) * hist.length)));
      let lo = Infinity;
      let hi = -Infinity;
      for (let i = i0; i < i1; i++) {
        const v = hist[i];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
      }
      if (lo === Infinity) continue;
      const yLo = midY - lo * (ch * 0.42);
      const yHi = midY - hi * (ch * 0.42);
      if (x === 0) ctx.moveTo(x, yLo);
      ctx.lineTo(x, yHi);
    }
    ctx.strokeStyle = '#4fc3f7';
    ctx.lineWidth = large ? 1.8 : 1.2;
    ctx.stroke();
  }, [samples, large]);

  return (
    <div className="probe-body">
      <canvas
        ref={ref}
        width={W}
        height={H}
        style={{ width: '100%', height: H, display: 'block', borderRadius: 4 }}
      />
    </div>
  );
}

export const NODE_WIDGETS = {
  'orpheus.builtin.probe_rms': ProbeRmsWidget,
  'orpheus.builtin.probe_peak': ProbePeakWidget,
  'orpheus.builtin.probe_waveform': ScopeWidget,
};
