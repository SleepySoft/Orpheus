import React, { useMemo, useState } from 'react';
import { subCatalogEntry } from './graphUtils';

// preferred display order for categories; unknown ones go last
const CATEGORY_ORDER = ['信号源', '基础算法', '通道路由', '文件', '设备', '监控工具'];

function categorySort(a, b) {
  const ia = CATEGORY_ORDER.indexOf(a);
  const ib = CATEGORY_ORDER.indexOf(b);
  if (ia !== -1 || ib !== -1) return (ia === -1 ? 99 : ia) - (ib === -1 ? 99 : ib);
  return a.localeCompare(b, 'zh');
}

/** Left-hand palette: project subcomponents + category tree of global components. */
export default function Palette({ components, subsMeta, onDeleteSub }) {
  const [collapsed, setCollapsed] = useState({});

  const byCategory = useMemo(() => {
    const m = new Map();
    for (const c of components) {
      const cat = c.category || '未分类';
      if (!m.has(cat)) m.set(cat, []);
      m.get(cat).push(c);
    }
    return [...m.entries()].sort((a, b) => categorySort(a[0], b[0]));
  }, [components]);

  const onDragStart = (event, componentId) => {
    event.dataTransfer.setData('application/orpheus-component', componentId);
    event.dataTransfer.effectAllowed = 'move';
  };

  const toggle = (cat) => setCollapsed((prev) => ({ ...prev, [cat]: !prev[cat] }));

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
        <div className="palette-category">
          <div className="palette-category-header static">工程子组件</div>
          {subsMeta.map((s) => renderItem(subCatalogEntry(s), true))}
        </div>
      )}
      {byCategory.map(([cat, items]) => (
        <div key={cat} className="palette-category">
          <div className="palette-category-header" onClick={() => toggle(cat)}>
            <span className="palette-caret">{collapsed[cat] ? '▶' : '▼'}</span>
            {cat}
            <span className="palette-count">{items.length}</span>
          </div>
          {!collapsed[cat] && items.map((c) => renderItem(c))}
        </div>
      ))}
      {components.length === 0 && <p className="muted">后端未连接或无组件</p>}
    </div>
  );
}
