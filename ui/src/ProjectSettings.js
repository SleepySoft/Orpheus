import React, { useState } from 'react';

/**
 * Project-level global settings modal: sample rate, block size, and the
 * realtime async ring-buffer size. These are graph-wide compile/runtime params.
 *
 * - sample_rate / block_size: the graph's compile-time rate and scheduling
 *   quantum. A device source may declare its own sample_rate (device_in /
 *   device_out params) which overrides this default at compile time; rt_host
 *   validates the effective rate against the device's native formats.
 * - buffer_size: realtime async ring-buffer capacity in frames (0 = auto,
 *   ~100 ms). Larger buffers tolerate more clock drift / scheduling jitter.
 */
export default function ProjectSettings({ doc, onSave, onClose }) {
  const [sampleRate, setSampleRate] = useState(String(doc?.sample_rate ?? 48000));
  const [blockSize, setBlockSize] = useState(String(doc?.block_size ?? 128));
  const [bufferSize, setBufferSize] = useState(String(doc?.buffer_size ?? 0));
  const [error, setError] = useState('');

  const save = () => {
    const sr = parseInt(sampleRate, 10);
    const bs = parseInt(blockSize, 10);
    const buf = parseInt(bufferSize, 10);
    if (!Number.isFinite(sr) || sr < 1000 || sr > 192000) {
      setError('采样率范围 1000-192000 Hz');
      return;
    }
    if (!Number.isFinite(bs) || bs < 1 || bs > 8192) {
      setError('块长度范围 1-8192');
      return;
    }
    if (!Number.isFinite(buf) || buf < 0 || buf > 1048576) {
      setError('缓冲大小范围 0-1048576（0=自动）');
      return;
    }
    onSave({ sample_rate: sr, block_size: bs, buffer_size: buf });
  };

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal" style={{ width: 460 }} onClick={(e) => e.stopPropagation()}>
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
          <span className="settings-hint">图形调度量子，影响延迟与 CPU。设备周期与之解耦（按块分片处理）。</span>
        </div>
        <div className="settings-field">
          <label>实时缓冲 buffer_size (帧)</label>
          <input type="number" value={bufferSize}
            onChange={(e) => setBufferSize(e.target.value)} />
          <span className="settings-hint">实时异步环形缓冲容量，0=自动（约 100ms）。越大越能容忍时钟漂移与调度抖动。</span>
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