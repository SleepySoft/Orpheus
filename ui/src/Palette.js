import React from 'react';
import { subCatalogEntry } from './graphUtils';

/** Left-hand palette: project subcomponents (draggable) + global components. */
export default function Palette({ components, subsMeta, onDeleteSub }) {
  const onDragStart = (event, componentId) => {
    event.dataTransfer.setData('application/orpheus-component', componentId);
    event.dataTransfer.effectAllowed = 'move';
  };

  const renderItem = (c, deletable = false) => (
    <div
      key={c.id}
      className={`palette-item ${c.sub ? 'sub' : ''}`}
      draggable
      onDragStart={(e) => onDragStart(e, c.id)}
      title={c.description || c.id}
    >
      <div className="palette-item-name">
        {c.name || c.id.split('.').pop()}
        {deletable && (
          <span
            className="palette-item-del"
            title="删除子组件（无实例引用时可用）"
            onClick={(e) => {
              e.stopPropagation();
              onDeleteSub(c.id);
            }}
          >
            ×
          </span>
        )}
      </div>
      <div className="palette-item-id">{c.id}</div>
    </div>
  );

  return (
    <div className="palette">
      {subsMeta.length > 0 && (
        <>
          <h3>工程子组件</h3>
          {subsMeta.map((s) => renderItem(subCatalogEntry(s), true))}
        </>
      )}
      <h3>组件库</h3>
      {components.map((c) => renderItem(c))}
      {components.length === 0 && <p className="muted">后端未连接或无组件</p>}
    </div>
  );
}
