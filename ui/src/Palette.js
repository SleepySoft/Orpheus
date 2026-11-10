import React, { useMemo, useState } from 'react';
import { subCatalogEntry, subViewKey } from './graphUtils';
import { fuzzyMatch } from './fuzzy';

// 通用组件按功能排列，工程分组始终置顶；未知分类仍可显示。
const TOP_ORDER = [
  '输入与输出', '驱动与缓冲', '路由与混音', '增益与动态', '滤波与均衡',
  '频域与分析', '自适应与降噪', '数学与控制', '平台与通信',
];

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

// 导入组件仍可使用多级分类；通用库按功能平铺，避免碎片化目录。
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
export default function Palette({ components, subsMeta, onDeleteSub, onDeleteComponent, onPromoteComponent, onShowReadme, readOnly = false }) {
  const [collapsed, setCollapsed] = useState({ '@project-subs': true, '@project-custom': true });
  const [query, setQuery] = useState('');

  const projectSubs = useMemo(
    () => subsMeta.map((sub) => ({ item: subCatalogEntry(sub), deletable: !sub.read_only })),
    [subsMeta]
  );
  const visibleComponents = useMemo(() => {
    const subIds = new Set(projectSubs.map(({ item }) => item.id));
    return components.filter((component) => !component.ui_hidden && !subIds.has(subViewKey(component.id)));
  }, [components, projectSubs]);
  const projectCustom = useMemo(
    () => visibleComponents.filter((component) => component.project_local || component.user_owned).sort(leafSort),
    [visibleComponents]
  );
  const tree = useMemo(
    () => buildTree(visibleComponents.filter((component) => !component.project_local && !component.user_owned)),
    [visibleComponents]
  );

  const onDragStart = (event, componentId) => {
    event.dataTransfer.setData('application/orpheus-component', componentId);
    event.dataTransfer.effectAllowed = 'move';
  };

  const toggle = (path) => setCollapsed((prev) => ({ ...prev, [path]: !prev[path] }));

  // 模糊搜索：中文显示名 / 英文 id / 分类路径 / 描述均参与匹配；命中时平铺展示结果
  const flat = useMemo(
    () => [
      ...projectSubs,
      ...projectCustom.map((item) => ({ item, deletable: false })),
      ...flattenItems(tree).map((c) => ({ item: c, deletable: false })),
    ],
    [projectSubs, projectCustom, tree]
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
      draggable={!readOnly}
      onDragStart={(e) => onDragStart(e, c.id)}
      title={c.description || c.id}
    >
      <div className="palette-item-name">
        <span>{c.name || c.id.split('.').pop()}</span>
        <span className="palette-item-actions">
          {!c.sub && onShowReadme && (
            <button
              className="palette-item-info"
              title="查看组件说明"
              draggable={false}
              onMouseDown={(e) => e.stopPropagation()}
              onDragStart={(e) => e.stopPropagation()}
              onClick={(e) => {
                e.stopPropagation();
                onShowReadme(c.id);
              }}
            >
              ℹ
            </button>
          )}
          {deletable && !readOnly && (
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
        </span>
      </div>
      <div className="palette-item-id">{c.id}</div>
      {!c.sub && c.user_owned && !c.project_local && (
        <div className="palette-manage">
          <button
            disabled={readOnly}
            title="提升为公共库组件（之后不可直接删除）"
            onClick={(e) => {
              e.stopPropagation();
              onPromoteComponent(c.id);
            }}
          >
            提升
          </button>
          <button
            disabled={readOnly}
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

  const renderProjectSection = (name, path, entries) => entries.length > 0 && (
    <div className="palette-category" key={path}>
      <button
        type="button"
        className="palette-category-header section"
        aria-label={name}
        aria-expanded={!collapsed[path]}
        onClick={() => toggle(path)}
      >
        <span className="palette-caret">{collapsed[path] ? '▶' : '▼'}</span>
        {name}
        <span className="palette-count">{entries.length}</span>
      </button>
      {!collapsed[path] && entries.map(({ item, deletable }) => renderItem(item, deletable))}
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
          {results.map(({ item, deletable }) => renderItem(item, deletable))}
        </div>
      ) : (
        <>
      {renderProjectSection('工程子组件', '@project-subs', projectSubs)}
      {renderProjectSection('工程定制组件', '@project-custom', projectCustom.map((item) => ({ item, deletable: false })))}
      {tree.children.size > 0 && renderNode(tree, '', 0)}
      {visibleComponents.length === 0 && projectSubs.length === 0 && <p className="muted">后端未连接或无组件</p>}
        </>
      )}
    </div>
  );
}
