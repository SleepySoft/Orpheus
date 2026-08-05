import React, { useState } from 'react';
import { isSubRef } from './graphUtils';

/**
 * Interface port editor shown in subcomponent views: list external ports and
 * add/remove them. Each port maps to an internal atomic node's port.
 */
export default function SubPortsPanel({ sub, viewNodes, catalogById, onAddPort, onRemovePort }) {
  const [direction, setDirection] = useState('input');
  const [mapsTo, setMapsTo] = useState('');

  // candidate internal endpoints: atomic nodes' ports matching the direction
  const options = [];
  for (const n of viewNodes) {
    if (isSubRef(n.data.component)) continue;
    const comp = catalogById[n.data.component];
    for (const p of comp?.ports || []) {
      if (p.direction === direction) options.push(`${n.id}:${p.id}`);
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
    </div>
  );
}
