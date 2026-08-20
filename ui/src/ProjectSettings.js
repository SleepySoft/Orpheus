import React, { useState, useEffect } from 'react';

/**
 * Project-level global settings modal.
 *
 * - sample_rate / block_size: graph compile-time rate and scheduling quantum.
 *   A device source may declare its own sample_rate (device_in/out params)
 *   which overrides the project default; rt_host validates the effective rate
 *   against the device's native formats.
 *
 * - buffer_size: realtime async ring-buffer capacity. Stored in FRAMES (0=auto
 *   = sample_rate/10 ~= 100ms). Configurable two ways, linked by the current
 *   sample rate:
 *     * by frame  : integer frames (the underlying unit).
 *     * by time   : milliseconds; converted to the nearest integer frame.
 *   Only one mode is editable at a time (radio). Editing the active field
 *   recomputes the other. Because frames must be integer, a ms value rounds to
 *   the nearest frame, so frame<->ms round-trips can drift by +/-1 frame.
 */
function fmtMs(frames, sr) {
  const f = parseInt(frames, 10) || 0;
  if (!sr) return '0';
  return String(Math.round((f * 1000 / sr) * 10) / 10);
}
function deriveFrames(ms, sr) {
  const m = parseFloat(ms);
  if (!Number.isFinite(m) || !sr) return 0;
  return Math.round(m * sr / 1000);
}

export default function ProjectSettings({ doc, onSave, onClose }) {
  const sr0 = parseInt(doc?.sample_rate ?? 48000, 10) || 48000;
  const [sampleRate, setSampleRate] = useState(String(sr0));
  const [blockSize, setBlockSize] = useState(String(doc?.block_size ?? 128));
  const [doubleBank, setDoubleBank] = useState(doc?.double_bank || 'auto');
  const [target, setTarget] = useState(doc?.target || 'auto');

  const frames0 = parseInt(doc?.buffer_size ?? 0, 10) || 0;
  const autoFrames0 = Math.round(sr0 / 10);
  const [bufAuto, setBufAuto] = useState(frames0 === 0);
  const [bufMode, setBufMode] = useState('frame'); // 'frame' | 'time'
  const [bufFrames, setBufFrames] = useState(String(frames0 === 0 ? autoFrames0 : frames0));
  const [bufMs, setBufMs] = useState(fmtMs(frames0 === 0 ? autoFrames0 : frames0, sr0));
  const [error, setError] = useState('');

  const sr = parseInt(sampleRate, 10) || 48000;

  // block_size in frames -> milliseconds (read-only hint). 3 decimals so power-of-2
  // sizes (128/256/512) do not look like round numbers and the relationship stays clear.
  const blockMs = (() => {
    const bs = parseInt(blockSize, 10);
    if (!Number.isFinite(bs) || bs <= 0 || !sr) return '—';
    return (bs * 1000 / sr).toFixed(3);
  })();

  // when sample rate changes, re-derive the displayed ms (frames is the truth)
  useEffect(() => {
    if (bufAuto) {
      const af = Math.round(sr / 10);
      setBufFrames(String(af));
      setBufMs(fmtMs(af, sr));
    } else {
      setBufMs(fmtMs(bufFrames, sr));
    }
  }, [sampleRate]);

  const onFramesChange = (v) => {
    setBufFrames(v);
    setBufMs(fmtMs(v, sr));
  };
  const onMsChange = (v) => {
    const f = deriveFrames(v, sr);
    setBufFrames(String(f));
    setBufMs(fmtMs(f, sr)); // snap ms to the exact frame equivalent
  };
  const onAutoToggle = (checked) => {
    setBufAuto(checked);
    if (checked) {
      const af = Math.round(sr / 10);
      setBufFrames(String(af));
      setBufMs(fmtMs(af, sr));
    }
  };

  const save = () => {
    const srVal = parseInt(sampleRate, 10);
    const bs = parseInt(blockSize, 10);
    if (!Number.isFinite(srVal) || srVal < 1000 || srVal > 192000) {
      setError('采样率范围 1000-192000 Hz');
      return;
    }
    if (!Number.isFinite(bs) || bs < 1 || bs > 8192) {
      setError('块长度范围 1-8192');
      return;
    }
    const buf = bufAuto ? 0 : parseInt(bufFrames, 10);
    if (!bufAuto && (!Number.isFinite(buf) || buf < 1 || buf > 1048576)) {
      setError('缓冲帧数范围 1-1048576');
      return;
    }
    onSave({ sample_rate: srVal, block_size: bs, buffer_size: buf, double_bank: doubleBank, target });
  };

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal" style={{ width: 480 }} onClick={(e) => e.stopPropagation()}>
        <h4>工程设置</h4>

        <div className="settings-field">
          <label>采样率 (Hz)</label>
          <input type="number" value={sampleRate}
            onChange={(e) => setSampleRate(e.target.value)} />
          <span className="settings-hint">图形编译期采样率。设备源可声明自身采样率覆盖此默认值，运行时按设备实际能力校验。</span>
        </div>

        <div className="settings-field">
          <label>块长度 block_size (帧)</label>
          <input type="number" value={blockSize}
            onChange={(e) => setBlockSize(e.target.value)} />
          <span className="settings-hint">图形调度量子，影响延迟与 CPU。设备周期与之解耦（按块分片处理）。当前块 ≈ {blockMs} ms（按工程采样率 {sr} Hz 换算：ms = 块帧数 / 采样率 × 1000；设备源可覆盖采样率，实际以运行为准）。</span>
        </div>

        <div className="settings-field">
          <label>实时缓冲 buffer_size</label>
          <label className="settings-check">
            <input type="checkbox" checked={bufAuto} onChange={(e) => onAutoToggle(e.target.checked)} />
            自动（= 采样率/10 ≈ 100ms，存储为 0）
          </label>
          <div className="buf-row">
            <label className="buf-mode">
              <input type="radio" checked={bufMode === 'frame'} disabled={bufAuto}
                onChange={() => setBufMode('frame')} />按帧
            </label>
            <input type="number" className="buf-input" value={bufFrames}
              disabled={bufAuto || bufMode !== 'frame'}
              onChange={(e) => onFramesChange(e.target.value)} />
            <span className="settings-hint">帧</span>
            <span className="buf-arrow">⇄</span>
            <label className="buf-mode">
              <input type="radio" checked={bufMode === 'time'} disabled={bufAuto}
                onChange={() => setBufMode('time')} />按时间
            </label>
            <input type="number" step="0.1" className="buf-input" value={bufMs}
              disabled={bufAuto || bufMode !== 'time'}
              onChange={(e) => onMsChange(e.target.value)} />
            <span className="settings-hint">ms</span>
          </div>
          <span className="settings-hint">
            底层以「帧」存储（0=自动=采样率/10≈100ms）。按帧/按时间二选一，按当前采样率联动：编辑一个，另一个自动换算。按时间输入四舍五入到最近整数帧，故帧↔毫秒来回切换可能 ±1 帧偏差（正常）。增大缓冲可容忍更多时钟漂移与调度抖动，但增加延迟。
          </span>
        </div>

        <div className="settings-field">
          <label>目标平台 (target)</label>
          <select value={target} onChange={(e) => setTarget(e.target.value)}>
            <option value="auto">自动（优先 win，整链交集判定）</option>
            <option value="win">win（PC/Windows）</option>
            <option value="dsp">dsp（嵌入式）</option>
          </select>
          <span className="settings-hint">
            目标平台决定生成代码的宿主形态：win = 可直连声卡运行的 PC 程序；dsp = 嵌入骨架 + platform_io.c 适配模板。
            alter 替代组按此激活成员（同组内任意平台只激活一个，未激活成员不参与编译）。
          </span>
        </div>

        <div className="settings-field">
          <label>BULK 双缓冲 (double_bank)</label>
          <select value={doubleBank} onChange={(e) => setDoubleBank(e.target.value)}>
            <option value="auto">自动（按组件声明）</option>
            <option value="on">全部开启（最安全，内存 ×2）</option>
            <option value="off">关闭（直写即时生效，部署省内存）</option>
          </select>
          <span className="settings-hint">
            双 bank = 写影子、块边界原子提交（无毛刺热更新系数）。部署内存紧张时选「关闭」，
            所有 BULK 槽改为直写 active、即时生效；自动模式按组件 manifest 的 double_bank 声明。
          </span>
        </div>

        {error && <div className="modal-error">{error}</div>}
        <div className="modal-actions">
          <button onClick={onClose}>取消</button>
          <button className="primary" onClick={save}>保存</button>
        </div>
      </div>
    </div>
  );
}
