import React, { useMemo, useState } from 'react';
import { subCatalogEntry } from './graphUtils';
import { fuzzyMatch } from './fuzzy';

// preferred display order for categories; unknown ones go last
const CATEGORY_ORDER = ['信号源', '基础算法', '通道路由', '文件', '设备', '监控工具'];

function categorySort(a, b) {
  const ia = CATEGORY_ORDER.indexOf(a);
  const ib = CATEGORY_ORDER.indexOf(b);
  if (ia !== -1 || ib !== -1) return (ia === -1 ? 99 : ia) - (ib === -1 ? 99 : ib);
  return a.localeCompare(b, 'zh');
}

/** Left-hand palette: project subcomponents + category tree of global components. */
export default function Palette({ components, subsMeta, onDeleteSub, onDeleteComponent, onPromoteComponent }) {
  const [collapsed, setCollapsed] = useState({});
  const [query, setQuery] = useState('');

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

  // 模糊搜索：中文显示名 / 英文 id / 描述均参与匹配；命中时平铺展示结果
  const flat = useMemo(
    () => [
      ...subsMeta.map((s) => ({ item: subCatalogEntry(s), deletable: true, sub: true })),
      ...byCategory.flatMap(([, items]) => items.map((c) => ({ item: c, deletable: false }))),
    ],
    [subsMeta, byCategory]
  );
  const searching = query.trim().length > 0;
  const results = searching
    ? flat.filter(({ item }) =>
        fuzzyMatch(query, item.name, item.id, item.description || '')
      )
    : [];

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
      {!c.sub && c.user_owned && (
        <div className="palette-manage">
          <button
            title="提升为公共库组件（之后不可直接删除）"
            onClick={(e) => {
              e.stopPropagation();
              onPromoteComponent(c.id);
            }}
          >
            提升
          </button>
          <button
            className="palette-del-btn"
            title="删除自定义组件（移除源码目录，需确认）"
            onClick={(e) => {
              e.stopPropagation();
              onDeleteComponent(c.id);
            }}
          >
            删除
          </button>
        </div>
      )}
    </div>
  );

  return (
    <div className="palette">
      <input
        className="palette-search"
        placeholder="搜索组件（中/英文模糊）"
        value={query}
        onChange={(e) => setQuery(e.target.value)}
      />
      {searching ? (
        <div className="palette-category">
          <div className="palette-category-header static">搜索结果（{results.length}）</div>
          {results.length === 0 && <p className="muted">无匹配组件</p>}
          {results.map(({ item, deletable, sub }) =>
            renderItem(
              sub ? { ...item, sub: true, user_owned: false } : item,
              deletable
            )
          )}
        </div>
      ) : (
        <>
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
        </>
      )}
    </div>
  );
}
