import React, { useState } from 'react';
import { isSubRef, resolvePorts } from './graphUtils';

/**
 * Interface port editor shown in subcomponent views: list external ports and
 * add/remove them. Each port maps to an internal atomic node's port.
 */
export default function SubPortsPanel({
  sub, viewNodes, catalogById, onAddPort, onRemovePort, onAddParameter, onRemoveParameter,
}) {
  const [direction, setDirection] = useState('input');
  const [mapsTo, setMapsTo] = useState('');
  const [paramDirection, setParamDirection] = useState('input');
  const [paramMapsTo, setParamMapsTo] = useState('');

  // candidate internal endpoints: atomic nodes' ports matching the direction
  // (resolved per node params so variable pins like out0..outN-1 appear)
  const options = [];
  for (const n of viewNodes) {
    if (isSubRef(n.data.component)) continue;
    const comp = catalogById[n.data.component];
    for (const p of resolvePorts(comp, n.data.params)) {
      if (p.direction === direction) options.push(`${n.id}:${p.id}`);
    }
  }

  const parameterOptions = [];
  for (const n of viewNodes) {
    if (isSubRef(n.data.component)) continue;
    for (const p of n.data.parameters || []) {
      const allowed = paramDirection === 'input'
        ? p.bindable && !p.affects_signature
        : p.control_source && (p.readback || p.kind === 'probe' || p.kind === 'state');
      if (allowed) parameterOptions.push({ value: `${n.id}:${p.id}`, schema: p });
    }
  }

  return (
    <div className="subports">
      <h4>接口端口（{sub.name}）</h4>
      {sub.ports.length === 0 && <p className="muted">尚无端口。添加端口后上层才能连线。</p>}
      {sub.ports.map((p) => (
        <div key={p.id} className="subport-row">
          <span className={`subport-dir ${p.direction}`}>
            {p.direction === 'input' ? '入' : '出'}
          </span>
          <span className="subport-id">{p.id}</span>
          <span className="subport-maps">→ {p.maps_to}</span>
          <button className="subport-del" title="删除端口" onClick={() => onRemovePort(p.id)}>
            ×
          </button>
        </div>
      ))}
      <div className="subport-add">
        <select value={direction} onChange={(e) => { setDirection(e.target.value); setMapsTo(''); }}>
          <option value="input">输入</option>
          <option value="output">输出</option>
        </select>
        <select value={mapsTo} onChange={(e) => setMapsTo(e.target.value)}>
          <option value="">映射到…</option>
          {options.map((o) => (
            <option key={o} value={o}>
              {o}
            </option>
          ))}
        </select>
        <button disabled={!mapsTo} onClick={() => { onAddPort(direction, mapsTo); setMapsTo(''); }}>
          添加
        </button>
      </div>
      <h4>公开参数</h4>
      {(sub.public_parameters || []).length === 0 && <p className="muted">尚无公开参数。</p>}
      {(sub.public_parameters || []).map((p) => (
        <div key={p.id} className="subport-row">
          <span className={`subport-dir ${p.direction}`}>{p.direction === 'input' ? '控入' : '控出'}</span>
          <span className="subport-id">{p.id}</span>
          <span className="subport-maps">→ {p.maps_to}</span>
          <button className="subport-del" title="删除公开参数" onClick={() => onRemoveParameter(p.id)}>×</button>
        </div>
      ))}
      <div className="subport-add">
        <select value={paramDirection} onChange={(e) => { setParamDirection(e.target.value); setParamMapsTo(''); }}>
          <option value="input">控制输入</option>
          <option value="output">控制输出</option>
        </select>
        <select value={paramMapsTo} onChange={(e) => setParamMapsTo(e.target.value)}>
          <option value="">映射到参数…</option>
          {parameterOptions.map((option) => (
            <option key={option.value} value={option.value}>{option.value}</option>
          ))}
        </select>
        <button
          disabled={!paramMapsTo}
          onClick={() => {
            const option = parameterOptions.find((candidate) => candidate.value === paramMapsTo);
            onAddParameter(paramDirection, paramMapsTo, option?.schema || {});
            setParamMapsTo('');
          }}
        >
          添加
        </button>
      </div>
    </div>
  );
}
