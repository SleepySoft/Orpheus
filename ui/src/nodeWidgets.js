import React from 'react';

/** 让 canvas 跟随容器尺寸（节点拖大 / 放大弹层都生效） */
function useCanvasSize(large, fw, fh) {
  const wrapRef = React.useRef(null);
  const [dim, setDim] = React.useState({ w: fw, h: fh });
  React.useEffect(() => {
    const el = wrapRef.current;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      const r = entries[0].contentRect;
      const w = Math.max(60, Math.round(r.width));
      const h = Math.max(40, Math.round(r.height));
      setDim((prev) => (prev.w === w && prev.h === h ? prev : { w, h }));
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, [large]);
  return { wrapRef, w: dim.w, h: dim.h };
}

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

/** 扫频发生器本体：进度条 + 当前输出频率（判断到底谁在工作） */
function SweepGenWidget({ data, large }) {
  const progress = data.probe?.progress;
  const freq = data.probe?.current_freq;
  const done = progress !== undefined && progress >= 0.999;
  const pct = Math.max(0, Math.min(100, (progress ?? 0) * 100));
  let freqText = '—';
  if (freq !== undefined) {
    if (freq <= 0) freqText = '已结束';
    else if (freq >= 1000) freqText = `${(freq / 1000).toFixed(2)} kHz`;
    else freqText = `${freq.toFixed(1)} Hz`;
  }
  return (
    <div className="probe-body">
      <div className="sweep-progress" title="扫频进度">
        <div className="sweep-progress-fill" style={{ width: `${pct}%` }} />
      </div>
      <div className="sweep-gen-meta">
        <span>当前 {freqText}</span>
        <span className={done ? 'sweep-done' : ''}>
          {progress === undefined ? '运行后显示' : done ? '完成' : `进度 ${Math.round(pct)}%`}
        </span>
      </div>
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
  const { wrapRef, w, h } = useCanvasSize(large, large ? 640 : 180, large ? 240 : 64);

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
  }, [samples, large, w, h]);

  return (
    <div className="probe-body">
      <div ref={wrapRef} className="monitor-widget">
        <canvas
          ref={ref}
          width={w}
          height={h}
          style={{ width: '100%', height: '100%', display: 'block', borderRadius: 4 }}
        />
      </div>
    </div>
  );
}

/**
 * Frequency spectrum widget: renders data.probe.spectrum (magnitude bins from
 * the probe_spectrum component). Freq axis derived from the compiled node rate
 * and the FFT window size parameter.
 */
function SpectrumWidget({ data, large }) {
  const bins = data.probe?.spectrum;
  const ref = React.useRef(null);
  const { wrapRef, w, h } = useCanvasSize(large, large ? 640 : 180, large ? 200 : 64);
  const windowSize = data.params?.window_size ?? 1024;
  const sampleRate = data.rate?.sample_rate ?? 48000;

  React.useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const cw = canvas.width;
    const ch = canvas.height;
    ctx.clearRect(0, 0, cw, ch);

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

    if (large) {
      ctx.fillStyle = 'rgba(255,255,255,0.45)';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'left';
      for (let i = 0; i <= 4; i++) {
        const db = -i * 20;
        ctx.fillText(`${db} dB`, 4, (ch * i) / 4 - 3);
      }
    }

    if (!Array.isArray(bins) || bins.length === 0) {
      ctx.fillStyle = 'rgba(255,255,255,0.45)';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('运行后显示频谱', cw / 2, ch / 2 + 3);
      return;
    }

    const n = bins.length;
    const nyquist = sampleRate / 2;
    const maxDb = 0;
    const minDb = -80;
    const toY = (db) => {
      const t = Math.max(0, Math.min(1, (db - minDb) / (maxDb - minDb)));
      return ch - 2 - t * (ch - 6);
    };
    const xFor = (i) => (i / n) * cw;

    // bars: dB-scaled magnitude, log-ish look via per-bin bars
    ctx.fillStyle = '#4fc3f7';
    const barW = Math.max(1, cw / n);
    for (let i = 0; i < n; i++) {
      const v = bins[i];
      const db = v > 1e-6 ? 20 * Math.log10(v) : minDb;
      const y = toY(db);
      ctx.fillRect(xFor(i), y, barW, ch - 2 - y);
    }

    if (large) {
      // frequency axis labels: 0, nyquist/2, nyquist
      ctx.fillStyle = 'rgba(255,255,255,0.45)';
      ctx.textAlign = 'center';
      const fmt = (hz) => (hz >= 1000 ? `${(hz / 1000).toFixed(1)}kHz` : `${hz}Hz`);
      ctx.fillText('0', 0, ch - 2);
      ctx.fillText(fmt(nyquist / 2), cw / 2, ch - 2);
      ctx.fillText(fmt(nyquist), cw - 2, ch - 2);
    }
  }, [bins, large, windowSize, sampleRate, w, h]);

  return (
    <div className="probe-body">
      <div ref={wrapRef} className="monitor-widget">
        <canvas
          ref={ref}
          width={w}
          height={h}
          style={{ width: '100%', height: '100%', display: 'block', borderRadius: 4 }}
        />
      </div>
    </div>
  );
}

function SweepPlotWidget({ data, large }) {
  const sweep = data.probe?.sweep;
  const { wrapRef, w, h } = useCanvasSize(large, large ? 640 : 220, large ? 260 : 90);
  const ref = React.useRef(null);

  React.useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const cw = canvas.width, ch = canvas.height;
    ctx.clearRect(0, 0, cw, ch);
    ctx.fillStyle = '#0d1117';
    ctx.fillRect(0, 0, cw, ch);

    const freq = sweep?.freq, mag = sweep?.mag;
    if (!Array.isArray(freq) || freq.length < 2 || !Array.isArray(mag) || mag.length < 2) {
      ctx.fillStyle = 'rgba(255,255,255,0.45)';
      ctx.font = '11px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('等待扫频数据…', cw / 2, ch / 2);
      return;
    }

    // 只统计已采集的箱（mag>0），未扫到的箱不参与 y 轴范围，避免曲线被压扁
    const db = mag.map((m) => (m > 0 ? 20 * Math.log10(m) : null));
    const measured = db.filter((d) => d !== null);
    let minF = Math.log10(freq[0]), maxF = Math.log10(freq[freq.length - 1]);
    if (!isFinite(minF) || !isFinite(maxF) || maxF <= minF) { minF = 1; maxF = 4; }
    let minD = measured.length ? Math.min(...measured) : -60;
    let maxD = measured.length ? Math.max(...measured) : 0;
    if (!isFinite(minD) || !isFinite(maxD) || maxD - minD < 1) { minD = -60; maxD = 0; }

    // 坐标区留边：左=dB 刻度，下=频率刻度，两种尺寸都显示
    const plotL = 42, plotR = cw - 8, plotT = 10, plotB = ch - 18;
    const px = (f) => plotL + ((Math.log10(f) - minF) / (maxF - minF)) * (plotR - plotL);
    const py = (d) => plotT + (1 - (d - minD) / (maxD - minD)) * (plotB - plotT);

    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
      const y = plotT + ((plotB - plotT) * i) / 4;
      ctx.beginPath(); ctx.moveTo(plotL, y); ctx.lineTo(plotR, y); ctx.stroke();
    }

    // 幅度轴（dB）刻度：5 格
    ctx.fillStyle = 'rgba(255,255,255,0.5)';
    ctx.font = '9px sans-serif';
    ctx.textAlign = 'right';
    for (let i = 0; i <= 4; i++) {
      const d = maxD - ((maxD - minD) * i) / 4;
      ctx.fillText(d.toFixed(0) + 'dB', plotL - 4, plotT + ((plotB - plotT) * i) / 4 + 3);
    }

    // 频率轴（对数）刻度：按数量级（10^n）落格，Hz/kHz 自适应
    const fmtHz = (f) => (f >= 1000 ? `${(f / 1000).toFixed(f >= 10000 ? 0 : 1)}k` : `${Math.round(f)}`);
    ctx.textAlign = 'center';
    const k0 = Math.floor(minF), k1 = Math.ceil(maxF);
    for (let k = k0; k <= k1; k++) {
      const f = Math.pow(10, k);
      if (f < freq[0] || f > freq[freq.length - 1]) continue;
      const x = px(f);
      ctx.beginPath(); ctx.moveTo(x, plotT); ctx.lineTo(x, plotB); ctx.stroke();
      ctx.fillText(fmtHz(f), x, ch - 5);
    }
    // 坐标轴主线
    ctx.strokeStyle = 'rgba(255,255,255,0.35)';
    ctx.beginPath(); ctx.moveTo(plotL, plotB); ctx.lineTo(plotR, plotB); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(plotL, plotT); ctx.lineTo(plotL, plotB); ctx.stroke();

    ctx.beginPath();
    ctx.strokeStyle = '#4cc9f0';
    ctx.lineWidth = large ? 2 : 1.2;
    let drawing = false;
    for (let i = 0; i < db.length; i++) {
      if (db[i] === null) { drawing = false; continue; }
      const x = px(freq[i]), y = py(db[i]);
      if (!drawing) { ctx.moveTo(x, y); drawing = true; } else { ctx.lineTo(x, y); }
    }
    ctx.stroke();

    ctx.fillStyle = 'rgba(255,255,255,0.55)';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'left';
    const prog = sweep.done ? '完成' : `扫频 ${Math.round((sweep.progress || 0) * 100)}%`;
    ctx.fillText(prog, plotL + 2, plotT + 10);
  }, [sweep, large, w, h]);

  return (
    <div ref={wrapRef} className="monitor-widget">
      <canvas
        ref={ref}
        width={w}
        height={h}
        style={{ width: '100%', height: '100%', display: 'block', borderRadius: 4 }}
      />
    </div>
  );
}

/**
 * Coherence matrix heatmap: renders data.probe.coherence = {n, matrix:[n*n]}
 * (0..1 values, blue=low red=high). Produced by coherence_matrix component.
 */
function HeatmapWidget({ data, large }) {
  const coh = data.probe?.coherence;
  const { wrapRef, w, h } = useCanvasSize(large, large ? 360 : 150, large ? 360 : 150);
  const ref = React.useRef(null);

  React.useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const cw = canvas.width;
    const ch = canvas.height;
    ctx.clearRect(0, 0, cw, ch);
    ctx.fillStyle = '#0d1117';
    ctx.fillRect(0, 0, cw, ch);

    const n = coh?.n;
    const matrix = coh?.matrix;
    if (!n || !Array.isArray(matrix) || matrix.length !== n * n) {
      ctx.fillStyle = 'rgba(255,255,255,0.45)';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('运行后显示相干矩阵', cw / 2, ch / 2);
      return;
    }
    const cell = Math.min(cw, ch) / n;
    for (let i = 0; i < n; i++) {
      for (let j = 0; j < n; j++) {
        const c = Math.max(0, Math.min(1, matrix[i * n + j]));
        const r = Math.round(255 * c);
        const g = Math.round(255 * (1 - c));
        const b = Math.round(255 * (1 - c));
        ctx.fillStyle = `rgb(${r},${g},${b})`;
        ctx.fillRect(j * cell, i * cell, cell + 0.5, cell + 0.5);
      }
    }
    if (large) {
      ctx.fillStyle = 'rgba(255,255,255,0.7)';
      ctx.font = '9px sans-serif';
      ctx.textAlign = 'center';
      for (let i = 0; i < n; i++) {
        ctx.fillText(String(i), i * cell + cell / 2, 10);
        ctx.fillText(String(i), 10, i * cell + cell / 2);
      }
    }
  }, [coh, large, w, h]);

  return (
    <div className="probe-body">
      <div ref={wrapRef} className="monitor-widget">
        <canvas
          ref={ref}
          width={w}
          height={h}
          style={{ width: '100%', height: '100%', display: 'block', borderRadius: 4 }}
        />
      </div>
    </div>
  );
}

/**
 * Control-value history curve: renders data.probe.history (array of recent
 * scalar values). Produced by interp_lut / coherence_matrix history readback.
 */
function TimeCurveWidget({ data, large }) {
  const hist = data.probe?.history;
  const { wrapRef, w, h } = useCanvasSize(large, large ? 640 : 200, large ? 200 : 64);
  const ref = React.useRef(null);

  React.useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const cw = canvas.width;
    const ch = canvas.height;
    ctx.clearRect(0, 0, cw, ch);
    ctx.fillStyle = '#0d1117';
    ctx.fillRect(0, 0, cw, ch);

    if (!Array.isArray(hist) || hist.length === 0) {
      ctx.fillStyle = 'rgba(255,255,255,0.45)';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('运行后显示控制值历史', cw / 2, ch / 2);
      return;
    }
    let min = Math.min(...hist);
    let max = Math.max(...hist);
    if (max - min < 1e-6) {
      min -= 0.5;
      max += 0.5;
    }
    const xFor = (i) => (i / (hist.length - 1)) * cw;
    const yFor = (v) => ch - 2 - ((v - min) / (max - min)) * (ch - 8);
    ctx.strokeStyle = '#4fc3f7';
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (let i = 0; i < hist.length; i++) {
      const x = xFor(i);
      const y = yFor(hist[i]);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
    if (large) {
      ctx.fillStyle = 'rgba(255,255,255,0.45)';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'right';
      ctx.fillText(max.toFixed(3), cw - 4, 10);
      ctx.fillText(min.toFixed(3), cw - 4, ch - 4);
    }
  }, [hist, large, w, h]);

  return (
    <div className="probe-body">
      <div ref={wrapRef} className="monitor-widget">
        <canvas
          ref={ref}
          width={w}
          height={h}
          style={{ width: '100%', height: '100%', display: 'block', borderRadius: 4 }}
        />
      </div>
    </div>
  );
}


/** Noise detector (single-ended): show flatness / noise floor / clicks / clip. */
function NoiseDetectorWidget({ data, large }) {
  const p = data.probe || {};
  const rows = [
    ['?????', p.flatness, 'flatness', 0],
    ['???(dB)', p.noise_floor_db, 'floor', 1],
    ['????', p.clicks, 'clicks', 0],
    ['????', p.clip_pct, 'clip', 1],
  ];
  return (
    <div className="probe-body">
      {rows.map(([label, v, key, isDb]) => {
        const present = v !== undefined;
        let text = present ? (isDb ? (v > 0 ? v.toFixed(1) : '-inf') : v.toFixed(2)) : '?';
        if (key === 'clicks' && present) text = String(v);
        if (key === 'clip' && present) text = `${(v * 100).toFixed(1)}%`;
        return (
          <div className="probe-stat" key={key}>
            <span className="muted">{label}</span>
            <span className={isDb && v > -30 ? 'stat-hot' : ''}>{text}</span>
          </div>
        );
      })}
    </div>
  );
}

/** Noise detector (dual-ended A/B): show THD+N, noise ratio, frames, clicks. */
function NoiseDetectorAbWidget({ data, large }) {
  const p = data.probe || {};
  const rows = [
    ['THD+N(dB)', p.thd_n_db, 'thd', 1],
    ['????', p.noise_ratio, 'ratio', 0],
    ['????', p.noise_frames, 'frames', 0],
    ['????', p.clicks, 'clicks', 0],
  ];
  return (
    <div className="probe-body">
      {rows.map(([label, v, key, isDb]) => {
        const present = v !== undefined;
        let text = present ? (isDb ? (v > 0 ? v.toFixed(1) : '-inf') : v.toFixed(2)) : '?';
        if (key === 'frames' && present) text = String(v);
        if (key === 'clicks' && present) text = String(v);
        if (key === 'ratio' && present) text = `${(v * 100).toFixed(1)}%`;
        return (
          <div className="probe-stat" key={key}>
            <span className="muted">{label}</span>
            <span className={isDb && v > -30 ? 'stat-hot' : ''}>{text}</span>
          </div>
        );
      })}
    </div>
  );
}

export const NODE_WIDGETS = {
  'orpheus.builtin.probe_rms': ProbeRmsWidget,
  'orpheus.builtin.probe_peak': ProbePeakWidget,
  'orpheus.builtin.probe_waveform': ScopeWidget,
  'orpheus.builtin.probe_spectrum': SpectrumWidget,
  'orpheus.builtin.psd': SpectrumWidget,
  'orpheus.builtin.coherence_matrix': HeatmapWidget,
  'orpheus.builtin.interp_lut': TimeCurveWidget,
  'orpheus.builtin.sweep_record': SweepPlotWidget,
  'orpheus.builtin.sweep_gen': SweepGenWidget,
  'orpheus.builtin.noise_detector': NoiseDetectorWidget,
  'orpheus.builtin.noise_detector_ab': NoiseDetectorAbWidget,
};
