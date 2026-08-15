import React, { useMemo, useState } from 'react';
import { subCatalogEntry } from './graphUtils';
import { fuzzyMatch } from './fuzzy';

// preferred display order for top-level categories; unknown ones go last (alphabetical)
const TOP_ORDER = ['基础', '音效', '高级', '平台'];

function topSort(a, b) {
  const ia = TOP_ORDER.indexOf(a);
  const ib = TOP_ORDER.indexOf(b);
  if (ia !== -1 || ib !== -1) return (ia === -1 ? 99 : ia) - (ib === -1 ? 99 : ib);
  return a.localeCompare(b, 'zh');
}

// 叶内排序：manifest order 字段小的在前（0=未指定排最后），同级按中文名
function leafSort(a, b) {
  const oa = a.order || 0;
  const ob = b.order || 0;
  if (oa !== ob) return (oa === 0 ? 999 : oa) - (ob === 0 ? 999 : ob);
  return (a.name || a.id).localeCompare(b.name || b.id, 'zh');
}

// category 为 '/' 分隔的多级路径（如 基础/滤波），递归建树，深度不限
function buildTree(components) {
  const root = { name: '', children: new Map(), items: [] };
  for (const c of components) {
    const segs = (c.category || '未分类').split('/').map((s) => s.trim()).filter(Boolean);
    let node = root;
    for (const seg of segs) {
      if (!node.children.has(seg)) node.children.set(seg, { name: seg, children: new Map(), items: [] });
      node = node.children.get(seg);
    }
    node.items.push(c);
  }
  return root;
}

function countItems(node) {
  let n = node.items.length;
  for (const ch of node.children.values()) n += countItems(ch);
  return n;
}

function flattenItems(node) {
  let out = [...node.items];
  for (const ch of node.children.values()) out = out.concat(flattenItems(ch));
  return out;
}

/** Left-hand palette: project subcomponents + category tree of global components. */
export default function Palette({ components, subsMeta, onDeleteSub, onDeleteComponent, onPromoteComponent }) {
  const [collapsed, setCollapsed] = useState({});
  const [query, setQuery] = useState('');

  const tree = useMemo(() => buildTree(components), [components]);

  const onDragStart = (event, componentId) => {
    event.dataTransfer.setData('application/orpheus-component', componentId);
    event.dataTransfer.effectAllowed = 'move';
  };

  const toggle = (path) => setCollapsed((prev) => ({ ...prev, [path]: !prev[path] }));

  // 模糊搜索：中文显示名 / 英文 id / 分类路径 / 描述均参与匹配；命中时平铺展示结果
  const flat = useMemo(
    () => [
      ...subsMeta.map((s) => ({ item: subCatalogEntry(s), deletable: true, sub: true })),
      ...flattenItems(tree).map((c) => ({ item: c, deletable: false })),
    ],
    [subsMeta, tree]
  );
  const searching = query.trim().length > 0;
  const results = searching
    ? flat.filter(({ item }) =>
        fuzzyMatch(query, item.name, item.id, item.category || '', item.description || '')
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

  // 递归渲染分类树：顶层按 TOP_ORDER，更深层按中文名；叶内按 order/名字
  const renderNode = (node, path, depth) => {
    const sorter = depth === 0 ? topSort : (a, b) => a.localeCompare(b, 'zh');
    const kids = [...node.children.values()].sort((a, b) => sorter(a.name, b.name));
    const items = [...node.items].sort(leafSort);
    return (
      <React.Fragment key={path}>
        {kids.map((ch) => {
          const cp = path ? `${path}/${ch.name}` : ch.name;
          const count = countItems(ch);
          return (
            <div key={cp} className="palette-category">
              <div
                className={`palette-category-header${depth > 0 ? ' sub' : ''}`}
                style={depth > 0 ? { marginLeft: depth * 10 } : undefined}
                onClick={() => toggle(cp)}
              >
                <span className="palette-caret">{collapsed[cp] ? '▶' : '▼'}</span>
                {ch.name}
                <span className="palette-count">{count}</span>
              </div>
              {!collapsed[cp] && renderNode(ch, cp, depth + 1)}
            </div>
          );
        })}
        {items.length > 0 && (
          <div style={depth > 0 ? { marginLeft: depth * 10 } : undefined}>
            {items.map((c) => renderItem(c))}
          </div>
        )}
      </React.Fragment>
    );
  };

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
      {tree.children.size > 0 && renderNode(tree, '', 0)}
      {components.length === 0 && <p className="muted">后端未连接或无组件</p>}
        </>
      )}
    </div>
  );
}
