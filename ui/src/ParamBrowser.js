import React, { useMemo, useRef, useState } from 'react';
import ParamField from './widgets';
import { isSubRef } from './graphUtils';

/**
 * 全局参数面板（树形 + 按数据类型分类）
 *
 * 树形结构：数据类型（调音参数 / 大块数据 / 探针 / …）→ 子组件层级 → 节点 → 数据点。
 * - 调音参数：可编辑，运行时非 restart_required 参数直接推 SET（flatId 即 flatten 后的节点地址）。
 * - Bulk 数据：manifest 声明 `kind: bulk` 的参数（如 FIR 系数），支持从文件导入 / 导出单个与整体。
 * - 探针：只读，优先显示实时值（rt.probes[flatId]）。
 * 每个节点条目带面包屑路径与「定位」按钮：点击跳转到对应画布标签页并选中节点。
 */

const KIND_ORDER = ['setting', 'bulk', 'probe', 'command', 'state'];
const KIND_META = {
  setting: { label: '调音参数', icon: '🎚️' },
  bulk: { label: '大块数据 (Bulk)', icon: '📦' },
  probe: { label: '探针', icon: '📈' },
  command: { label: '命令', icon: '⚡' },
  state: { label: '状态', icon: '🧩' },
};

/** 数据点类别：显式 kind 优先；否则按 readback 语义推断（与 ParamPanel 的 isDisplayOnly 一致）。 */
function kindOf(p) {
  if (p.kind) return p.kind;
  if (p.readback && !p.persistent && !p.affects_signature) return 'probe';
  return 'setting';
}

/** 从逗号/空白/分号分隔的文本解析浮点数组。 */
function parseFloatList(text) {
  const parts = String(text ?? '').split(/[\s,;]+/).filter(Boolean);
  const out = [];
  for (const p of parts) {
    const v = Number.parseFloat(p);
    if (Number.isFinite(v)) out.push(v);
  }
  return out;
}

function fmtProbe(v) {
  if (v === undefined || v === null) return '—';
  if (typeof v === 'number') return String(Math.round(v * 1e4) / 1e4);
  if (Array.isArray(v)) {
    const head = v
      .slice(0, 5)
      .map((x) => (typeof x === 'number' ? String(Math.round(x * 1e4) / 1e4) : String(x)))
      .join(', ');
    return `[${v.length} 点] ${head}${v.length > 5 ? ', …' : ''}`;
  }
  if (typeof v === 'object') return JSON.stringify(v).slice(0, 120);
  return String(v);
}

function downloadFile(name, content, mime) {
  const blob = new Blob([content], { type: mime || 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/** 单个 Bulk 数据行：文本编辑 + 文件导入/导出。 */
function BulkRow({ schema, value, onChange }) {
  const fileRef = useRef(null);
  const count = parseFloatList(value).length;

  const onFile = async (e) => {
    const f = e.target.files && e.target.files[0];
    e.target.value = '';
    if (!f) return;
    const text = await f.text();
    let arr = null;
    try {
      const j = JSON.parse(text);
      if (Array.isArray(j)) arr = j.map(Number).filter(Number.isFinite);
    } catch {
      /* 非 JSON，按文本数值解析 */
    }
    if (!arr) arr = parseFloatList(text);
    if (!arr.length) {
      window.alert('文件中没有可解析的数值（支持 JSON 数组或逗号/空格分隔的文本）');
      return;
    }
    onChange(schema.id, arr.join(', '));
  };

  return (
    <div className="pb-bulk">
      <input
        type="text"
        value={value ?? ''}
        title="逗号或空格分隔的数值"
        onChange={(e) => onChange(schema.id, e.target.value)}
      />
      <span className="pb-bulk-count">{count} 个值</span>
      <button onClick={() => fileRef.current && fileRef.current.click()}>从文件导入</button>
      <button
        onClick={() =>
          downloadFile(`${schema.id}.json`, JSON.stringify(parseFloatList(value), null, 2))
        }
        disabled={count === 0}
      >
        导出
      </button>
      <input
        ref={fileRef}
        type="file"
        accept=".json,.txt,.csv"
        style={{ display: 'none' }}
        onChange={onFile}
      />
    </div>
  );
}

/** 运行期 BULK 槽行（组件 register_slots 注册、仅实时会话可写）：文本 + 文件导入 + 写入会话。 */
function RuntimeBulkRow({ schema, running, onWrite }) {
  const [text, setText] = useState('');
  const fileRef = useRef(null);
  const vals = parseFloatList(text);
  const countOk = schema.count ? vals.length === schema.count : vals.length > 0;

  const onFile = async (e) => {
    const f = e.target.files && e.target.files[0];
    e.target.value = '';
    if (!f) return;
    const t = await f.text();
    let arr = null;
    try {
      const j = JSON.parse(t);
      if (Array.isArray(j)) arr = j.map(Number).filter(Number.isFinite);
    } catch {
      /* 非 JSON，按文本数值解析 */
    }
    if (!arr) arr = parseFloatList(t);
    if (!arr.length) {
      window.alert('文件中没有可解析的数值（支持 JSON 数组或逗号/空格分隔的文本）');
      return;
    }
    if (schema.count && arr.length !== schema.count) {
      window.alert(`需要 ${schema.count} 个值，文件里有 ${arr.length} 个`);
      return;
    }
    setText(arr.join(', '));
  };

  const write = () => {
    if (!countOk) {
      window.alert(`需要 ${schema.count} 个值，当前 ${vals.length} 个`);
      return;
    }
    onWrite(vals);
  };

  return (
    <div className="pb-bulk">
      <input
        type="text"
        placeholder={`${schema.count} 个数值，逗号/空格分隔`}
        value={text}
        onChange={(e) => setText(e.target.value)}
      />
      <span className="pb-bulk-count">
        {vals.length}/{schema.count}
      </span>
      <button onClick={() => fileRef.current && fileRef.current.click()}>从文件导入</button>
      <button
        className="pb-write"
        disabled={!running || !countOk}
        onClick={write}
        title={running ? '写入实时会话（BULK 协议直写槽内存）' : '仅实时会话可写'}
      >
        写入实时会话
      </button>
      <input
        ref={fileRef}
        type="file"
        accept=".json,.txt,.csv"
        style={{ display: 'none' }}
        onChange={onFile}
      />
    </div>
  );
}

export default function ParamBrowser({
  projectName,
  views,
  catalogById,
  rt,
  ctx,
  onNodeParamChange,
  onImportApply,
  onWriteBulk,
  presets,
  onSavePreset,
  onDeletePreset,
  onLocate,
  onClose,
}) {
  const [search, setSearch] = useState('');
  const [collapsed, setCollapsed] = useState({});
  const importFileRef = useRef(null);

  /** 遍历主图与子组件视图，产出扁平条目（flatId = 实例路径 join '__'，与 flatten_project 同规则）。 */
  const tree = useMemo(() => {
    const entries = [];
    const walk = (viewKey, path) => {
      const view = views[viewKey];
      if (!view || !view.nodes) return;
      for (const nd of view.nodes) {
        if (isSubRef(nd.data.component)) {
          if (path.some((p) => p.id === nd.id)) continue; // 环保护（后端 flatten 也会报错）
          walk(nd.data.component, [...path, { id: nd.id, label: nd.data.label || nd.id }]);
          continue;
        }
        const comp = catalogById[nd.data.component];
        const params = (comp && comp.parameters) || [];
        const byKind = { setting: [], bulk: [], probe: [], command: [], state: [] };
        for (const p of params) {
          (byKind[kindOf(p)] || byKind.setting).push(p);
        }
        for (const bs of (comp && comp.bulk_slots) || []) {
          byKind.bulk.push({ ...bs, runtime: true });
        }
        const nodePath = [...path, { id: nd.id, label: nd.data.label || nd.id }];
        entries.push({
          flatId: nodePath.map((x) => x.id).join('__'),
          viewKey,
          nodeId: nd.id,
          label: nd.data.label || nd.id,
          component: nd.data.component,
          componentName: (comp && comp.name) || nd.data.component,
          path: nodePath,
          byKind,
        });
      }
    };
    walk('main', []);
    return entries;
  }, [views, catalogById]);

  const q = search.trim().toLowerCase();
  const filtered = useMemo(() => {
    if (!q) return tree;
    return tree
      .map((e) => {
        const matchNode =
          e.label.toLowerCase().includes(q) ||
          e.componentName.toLowerCase().includes(q) ||
          e.flatId.toLowerCase().includes(q);
        const byKind = {};
        let any = matchNode;
        for (const k of KIND_ORDER) {
          byKind[k] = (e.byKind[k] || []).filter(
            (p) =>
              (p.name || '').toLowerCase().includes(q) || p.id.toLowerCase().includes(q)
          );
          if (byKind[k].length) any = true;
        }
        return any ? { ...e, byKind } : null;
      })
      .filter(Boolean);
  }, [tree, q]);

  const counts = useMemo(() => {
    const c = { setting: 0, bulk: 0, probe: 0, command: 0, state: 0 };
    for (const e of tree) for (const k of KIND_ORDER) c[k] += (e.byKind[k] || []).length;
    return c;
  }, [tree]);

  const nodeParams = (e) => {
    const view = views[e.viewKey];
    const nd = view && view.nodes.find((n) => n.id === e.nodeId);
    return (nd && nd.data.params) || {};
  };
  const nodeProbe = (e) => {
    const view = views[e.viewKey];
    const nd = view && view.nodes.find((n) => n.id === e.nodeId);
    return (nd && nd.data.probe) || {};
  };

  /** 构建调音快照（调音值 + 工程 Bulk；不含探针实时值与运行期槽）。 */
  const buildSnapshot = () => ({
    format: 'orpheus.parameters',
    version: 1,
    created_at: new Date().toISOString(),
    nodes: tree.map((e) => {
      const params = nodeParams(e);
      const values = {};
      const bulk = {};
      for (const p of e.byKind.setting || []) values[p.id] = params[p.id] ?? p.default;
      for (const p of e.byKind.bulk || []) {
        if (!p.runtime) bulk[p.id] = parseFloatList(params[p.id]);
      }
      return {
        node: e.flatId,
        path: e.path.map((x) => x.label),
        component: e.component,
        component_name: e.componentName,
        values,
        bulk,
      };
    }),
  });

  const exportAll = () => {
    const snap = buildSnapshot();
    const payload = {
      ...snap,
      project: projectName,
      exported_at: new Date().toISOString(),
      nodes: tree.map((e) => {
        const n = snap.nodes.find((x) => x.node === e.flatId);
        const probes = {};
        for (const p of e.byKind.probe || []) {
          const live = rt.probes?.[e.flatId]?.[p.id] ?? nodeProbe(e)[p.id];
          if (live !== undefined) probes[p.id] = live;
        }
        return { ...n, probes };
      }),
    };
    downloadFile(`${projectName || 'project'}-parameters.json`, JSON.stringify(payload, null, 2));
  };

  const savePreset = () => {
    const name = window.prompt('预设名称（保存当前全部调音参数与 Bulk 数据）:');
    if (!name || !name.trim()) return;
    onSavePreset(name.trim(), buildSnapshot());
  };

  const applyPreset = (preset) => {
    const byFlat = Object.fromEntries(tree.map((t) => [t.flatId, t]));
    const items = [];
    const missing = [];
    for (const n of preset.nodes || []) {
      const t = byFlat[n.node];
      if (!t) {
        missing.push(n.node);
        continue;
      }
      items.push({
        viewKey: t.viewKey,
        nodeId: t.nodeId,
        flatId: n.node,
        values: n.values || {},
        bulk: n.bulk || {},
      });
    }
    if (missing.length) {
      window.alert(`以下节点未找到，已跳过：${missing.join(', ')}`);
    }
    onImportApply(items);
  };

  const onImportFile = async (e) => {
    const f = e.target.files && e.target.files[0];
    e.target.value = '';
    if (!f) return;
    let payload;
    try {
      payload = JSON.parse(await f.text());
    } catch {
      window.alert('导入文件不是有效的 JSON');
      return;
    }
    if (!Array.isArray(payload.nodes)) {
      window.alert('导入文件缺少 nodes 数组（请使用本面板导出的 JSON）');
      return;
    }
    const byFlat = Object.fromEntries(tree.map((t) => [t.flatId, t]));
    const resolved = [];
    const missing = [];
    for (const n of payload.nodes) {
      const t = byFlat[n.node];
      if (!t) {
        missing.push(n.node);
        continue;
      }
      resolved.push({
        viewKey: t.viewKey,
        nodeId: t.nodeId,
        flatId: n.node,
        values: n.values || {},
        bulk: n.bulk || {},
      });
    }
    if (missing.length) {
      window.alert(`以下节点未找到，已跳过：${missing.join(', ')}`);
    }
    onImportApply(resolved);
  };

  const toggle = (key) => setCollapsed((c) => ({ ...c, [key]: !c[key] }));

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal param-browser" onClick={(e) => e.stopPropagation()}>
        <h4>参数面板</h4>
        <div className="pb-toolbar">
          <input
            className="pb-search"
            type="text"
            placeholder="搜索参数 / 节点 / 组件…"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
          />
          <button onClick={exportAll}>导出全部 (JSON)</button>
          <button onClick={() => importFileRef.current && importFileRef.current.click()}>
            导入 JSON
          </button>
          <input
            ref={importFileRef}
            type="file"
            accept=".json"
            style={{ display: 'none' }}
            onChange={onImportFile}
          />
          {rt.running && <span className="pb-live">⏺ 实时会话中：非重启参数修改即时生效</span>}
        </div>
        <div className="pb-presets">
          <strong>预设</strong>
          <button onClick={savePreset}>保存当前为预设</button>
          {!presets.length && <span className="pb-meta">暂无（预设随工程保存）</span>}
          {presets.map((p) => (
            <div key={p.name} className="pb-preset-row">
              <span className="pb-preset-name">{p.name}</span>
              <span className="pb-meta">{p.created_at ? new Date(p.created_at).toLocaleString() : ''}</span>
              <button onClick={() => applyPreset(p)}>应用</button>
              <button className="pb-preset-del" onClick={() => onDeletePreset(p.name)}>
                删除
              </button>
            </div>
          ))}
        </div>
        <div className="pb-body">
          {KIND_ORDER.map((kind) => {
            const entries = filtered.filter((e) => (e.byKind[kind] || []).length > 0);
            if (!entries.length) return null;
            const meta = KIND_META[kind];
            const isCollapsed = !!collapsed[kind];
            return (
              <div key={kind} className="pb-section">
                <div className="pb-section-head" onClick={() => toggle(kind)}>
                  <span className="pb-caret">{isCollapsed ? '▶' : '▼'}</span>
                  <span>
                    {meta.icon} {meta.label}
                  </span>
                  <span className="pb-count">{counts[kind]}</span>
                </div>
                {!isCollapsed &&
                  entries.map((e) => (
                    <div key={e.flatId} className="pb-entry">
                      <div className="pb-entry-head">
                        <span className="pb-breadcrumb" title={e.flatId}>
                          {e.path.map((x) => x.label).join(' › ')}
                        </span>
                        <span className="pb-comp">{e.componentName}</span>
                        <button className="pb-locate" onClick={() => onLocate(e.viewKey, e.nodeId)}>
                          定位
                        </button>
                      </div>
                      <div className="pb-rows">
                        {(e.byKind[kind] || []).map((p) => {
                          const value = nodeParams(e)[p.id] ?? p.default;
                          if (kind === 'probe') {
                            const live =
                              rt.probes?.[e.flatId]?.[p.id] ?? nodeProbe(e)[p.id];
                            return (
                              <div key={p.id} className="pb-row pb-probe">
                                <span className="pb-name">{p.name || p.id}</span>
                                <span className="pb-probe-value">{fmtProbe(live)}</span>
                                <span className="pb-meta">
                                  {p.unit ? `(${p.unit}) ` : ''}只读
                                </span>
                              </div>
                            );
                          }
                          if (kind === 'bulk') {
                            if (p.runtime) {
                              return (
                                <div key={p.id} className="pb-row">
                                  <span className="pb-name">
                                    {p.name || p.id}
                                    <span className="pb-meta"> 运行期槽</span>
                                  </span>
                                  <RuntimeBulkRow
                                    schema={p}
                                    running={rt.running}
                                    onWrite={(vals) => onWriteBulk(e.flatId, p.id, vals)}
                                  />
                                </div>
                              );
                            }
                            return (
                              <div key={p.id} className="pb-row">
                                <span className="pb-name">{p.name || p.id}</span>
                                <BulkRow
                                  schema={p}
                                  value={value}
                                  onChange={(id, v) =>
                                    onNodeParamChange(e.viewKey, e.nodeId, id, v, e.flatId)
                                  }
                                />
                              </div>
                            );
                          }
                          return (
                            <div key={p.id} className="pb-row">
                              <ParamField
                                schema={p}
                                value={value}
                                onChange={(id, v) =>
                                  onNodeParamChange(e.viewKey, e.nodeId, id, v, e.flatId)
                                }
                                ctx={ctx}
                              />
                            </div>
                          );
                        })}
                      </div>
                    </div>
                  ))}
              </div>
            );
          })}
          {!filtered.length && <p className="muted">没有匹配的数据点。</p>}
        </div>
        <div className="modal-actions">
          <button onClick={onClose}>关闭</button>
        </div>
      </div>
    </div>
  );
}
