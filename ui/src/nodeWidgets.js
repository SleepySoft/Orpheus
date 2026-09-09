import React from 'react';
import Plot from './plot/Plot';

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
  const histRef = React.useRef([]);
  const HISTORY_CAP = 8192; // ~85ms @48kHz; larger = smoother scroll, smaller = faster response
  const sampleRate = data.rate?.sample_rate ?? 48000;
  const hist = Array.isArray(samples) && samples.length
    ? [...(histRef.current || []), ...samples].slice(-HISTORY_CAP)
    : [];
  histRef.current = hist;
  const series = hist.length
    ? [{
      id: 'waveform',
      label: '输出',
      type: 'waveform',
      data: hist,
      color: '#4fc3f7',
      width: large ? 1.6 : 1.2,
    }]
    : [];

  return (
    <div className="probe-body">
      <Plot
        variant={large ? 'panel' : 'node'}
        series={series}
        x={{
          label: large ? '时间' : undefined,
          unit: 's',
          scale: 'linear',
          domain: [0, Math.max(1, hist.length - 1) / sampleRate],
        }}
        y={{ label: large ? '幅值' : undefined, unit: '', scale: 'linear', domain: [-1, 1] }}
        emptyText="运行后显示波形"
        legend={large}
      />
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
  const windowSize = data.params?.window_size ?? 1024;
  const sampleRate = data.rate?.sample_rate ?? 48000;
  const values = Array.isArray(bins) ? bins : [];
  const nyquist = sampleRate / 2;
  const binWidth = values.length ? sampleRate / windowSize : 0;
  const series = values.length
    ? [{
      id: 'spectrum',
      label: '频谱',
      type: 'bars',
      x: values.map((_, index) => index * binWidth),
      y: values.map((value) => (value > 1e-6 ? 20 * Math.log10(value) : -80)),
      color: '#4fc3f0',
    }]
    : [];

  return (
    <div className="probe-body">
      <Plot
        variant={large ? 'panel' : 'node'}
        series={series}
        x={{
          label: large ? '频率' : undefined,
          unit: 'Hz',
          scale: 'linear',
          domain: [0, Math.max(nyquist, values.length ? (values.length - 1) * binWidth : 0)],
          ticks: large ? 'auto' : [0, nyquist / 2, nyquist],
        }}
        y={{
          label: large ? '幅度' : undefined,
          unit: 'dB',
          scale: 'linear',
          domain: [-80, 0],
        }}
        emptyText="运行后显示频谱"
        legend={large}
      />
    </div>
  );
}

function SweepPlotWidget({ data, large }) {
  const sweep = data.probe?.sweep;
  const freq = Array.isArray(sweep?.freq) ? sweep.freq : [];
  const mag = Array.isArray(sweep?.mag) ? sweep.mag : [];
  const valid = freq.length > 1 && mag.length > 1;
  const db = valid ? mag.map((value) => (value > 0 ? 20 * Math.log10(value) : null)) : [];
  const measured = db.filter((value) => value !== null);
  const minD = measured.length ? Math.min(...measured) : -60;
  const maxD = measured.length ? Math.max(...measured) : 0;
  const series = valid
    ? [{
      id: 'sweep',
      label: '扫频响应',
      type: 'line',
      x: freq,
      y: db,
      color: '#4cc9f0',
      width: large ? 1.8 : 1.2,
    }]
    : [];

  return (
    <Plot
      variant={large ? 'panel' : 'node'}
      series={series}
      x={{ label: '频率', unit: 'Hz', scale: 'log' }}
      y={{ label: '幅度', unit: 'dB', domain: [minD, maxD] }}
      emptyText="等待扫频数据…"
      legend={large}
    />
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
  const values = Array.isArray(hist) ? hist : [];
  const series = values.length
    ? [{
      id: 'history',
      label: '控制值',
      type: 'line',
      x: values.map((_, index) => index),
      y: values,
      color: '#4cc9f0',
      width: large ? 1.6 : 1.2,
    }]
    : [];

  return (
    <div className="probe-body">
      <Plot
        variant={large ? 'panel' : 'node'}
        series={series}
        x={{ label: large ? '历史帧' : undefined, scale: 'linear' }}
        y={{ label: large ? '控制值' : undefined, scale: 'linear' }}
        emptyText="运行后显示控制值历史"
        legend={large}
      />
    </div>
  );
}


/** Noise detector (single-ended): show flatness / noise floor / clicks / clip. */
function NoiseDetectorWidget({ data, large }) {
  const p = data.probe || {};
  const rows = [
    ['频谱平坦度', p.flatness, 'flatness', 0],
    ['噪声底(dB)', p.noise_floor_db, 'floor', 1],
    ['突刺计数', p.clicks, 'clicks', 0],
    ['削波占比', p.clip_pct, 'clip', 1],
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
    ['噪声占比', p.noise_ratio, 'ratio', 0],
    ['噪声帧数', p.noise_frames, 'frames', 0],
    ['突刺计数', p.clicks, 'clicks', 0],
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


/** Noise detector (NLMS residual): show residue budget / ERLE / ratio / frames. */
function NoiseDetectorNlmsWidget({ data, large }) {
  const pN = data.probe || {};
  const rows = [
    ['残差(dB)', pN.residue_db, 'res', 0],
    ['ERLE(dB)', pN.erle_db, 'erle', 1],
    ['噪声占比', pN.noise_ratio, 'ratio', 0],
    ['噪声帧数', pN.noise_frames, 'frames', 0],
    ['突刺计数', pN.clicks, 'clicks', 0],
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
            <span className={key === 'res' && v > -15 ? 'stat-hot' : ''}>{text}</span>
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
  'orpheus.builtin.noise_detector_nlms': NoiseDetectorNlmsWidget,
};
