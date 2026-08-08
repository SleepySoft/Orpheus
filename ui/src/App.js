import React, { useState, useCallback, useEffect, useMemo, useRef } from 'react';
import ReactFlow, {
  Background,
  Controls,
  MiniMap,
  addEdge,
  applyNodeChanges,
  applyEdgeChanges,
  ReactFlowProvider,
  useReactFlow,
} from 'reactflow';
import 'reactflow/dist/style.css';

import './App.css';
import * as api from './api';
import {
  docToViews,
  viewsToDoc,
  mergedCatalog,
  defaultParams,
  resolvePorts,
  isSubRef,
  subIdOf,
  subViewKey,
} from './graphUtils';
import OrpheusNode from './OrpheusNode';
import ParamPanel from './ParamPanel';
import ParamBrowser from './ParamBrowser';
import Palette from './Palette';
import SubPortsPanel from './SubPortsPanel';
import ProjectSettings from './ProjectSettings';

const nodeTypes = { orpheus: OrpheusNode };
const AUTOSAVE_DELAY_MS = 1500;
const EMPTY_VIEW = { nodes: [], edges: [] };

const uniqueNodeId = (existing, base) => {
  const ids = new Set(existing.map((n) => n.id));
  let id = base;
  let n = 1;
  while (ids.has(id)) id = `${base}_${++n}`;
  return id;
};

function Editor() {
  const [catalog, setCatalog] = useState([]);
  const [projects, setProjects] = useState([]);
  const [examples, setExamples] = useState([]);
  const [current, setCurrent] = useState(null); // current project name
  const [doc, setDoc] = useState(null); // last saved/loaded document
  const [views, setViews] = useState({ main: EMPTY_VIEW });
  const [subsMeta, setSubsMeta] = useState([]);
  const [openTabs, setOpenTabs] = useState(['main']);
  const [activeView, setActiveView] = useState('main');
  const [selectedId, setSelectedId] = useState(null);
  const [selectedIds, setSelectedIds] = useState([]);
  const [paceRun, setPaceRun] = useState(false);
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const [autoSave, setAutoSave] = useState(true);
  const [showSettings, setShowSettings] = useState(false);
  const [showParams, setShowParams] = useState(false);
  const [leftOpen, setLeftOpen] = useState(true);   // 组件库
  const [rightOpen, setRightOpen] = useState(true); // 参数面板/子组件面板
  const [openMenu, setOpenMenu] = useState(null); // 'project' | 'run' | null：分组展开菜单
  const distillFileRef = useRef(null);
  const [status, setStatus] = useState('未连接后端');
  const [log, setLog] = useState(null);
  const [outputs, setOutputs] = useState([]);
  const [generatedInfo, setGeneratedInfo] = useState(null); // {path, url} 生成工程位置与下载链接
  const [idMap, setIdMap] = useState([]); // 编译响应携带的数据点 ID 表（0x ID / 用途/形式/类型）
  const [deviceOptions, setDeviceOptions] = useState([{ value: '', label: '默认设备' }]);
  const [rt, setRt] = useState({ running: false, logs: [], probes: {} });
  const [logCollapsed, setLogCollapsed] = useState(false);
  const [logHeight, setLogHeight] = useState(180);
const { screenToFlowPosition } = useReactFlow();

  // drag the log panel header vertically to resize it
  const onLogDragStart = useCallback(
    (e) => {
      e.preventDefault();
      const startY = e.clientY;
      const startH = logHeight;
      const onMove = (ev) =>
        setLogHeight(Math.min(window.innerHeight * 0.6, Math.max(80, startH + (startY - ev.clientY))));
      const onUp = () => {
        window.removeEventListener('mousemove', onMove);
        window.removeEventListener('mouseup', onUp);
      };
      window.addEventListener('mousemove', onMove);
      window.addEventListener('mouseup', onUp);
    },
    [logHeight]
  );

  const rtRef = useRef(rt);
  rtRef.current = rt;

  const paramCtx = useMemo(
    () => ({ projectName: current, dynamicOptions: { devices: deviceOptions } }),
    [current, deviceOptions]
  );

  const dirtyRef = useRef(dirty);
  dirtyRef.current = dirty;

  const fullCatalog = useMemo(() => mergedCatalog(catalog, subsMeta), [catalog, subsMeta]);
  const catalogById = useMemo(
    () => Object.fromEntries(fullCatalog.map((c) => [c.id, c])),
    [fullCatalog]
  );
  const view = views[activeView] || EMPTY_VIEW;
  const activeSubId = activeView === 'main' ? null : subIdOf(activeView);
  const activeSub = activeSubId ? subsMeta.find((s) => s.id === activeSubId) : null;

  const updateView = useCallback((key, updater) => {
    setViews((prev) => ({ ...prev, [key]: updater(prev[key] || EMPTY_VIEW) }));
  }, []);

  // ---------------------------------------------------------- data loading

  const refreshProjects = useCallback(async () => {
    try {
      setProjects(await api.listProjects());
    } catch (e) {
      setStatus(`后端连接失败: ${api.errorDetail(e)}`);
    }
  }, []);

  const loadDocument = useCallback((name, document, comps) => {
    const { views: v, subsMeta: sm } = docToViews(document, comps);
    setCurrent(name);
    setDoc(document);
    setViews(v);
    setSubsMeta(sm);
    setOpenTabs(['main']);
    setActiveView('main');
    setSelectedId(null);
    setSelectedIds([]);
    setDirty(false);
    setOutputs([]);
    setGeneratedInfo(null);
    setIdMap([]);
    setLog(null);
    setRt({ running: false, logs: [], probes: {} });
  }, []);

  const openProject = useCallback(
    async (name, presetDoc = null) => {
      try {
        const document = presetDoc || (await api.getProject(name));
        loadDocument(name, document, catalog);
        setStatus(`已打开工程 ${name}`);
      } catch (e) {
        setStatus(`打开工程失败: ${api.errorDetail(e)}`);
      }
    },
    [catalog, loadDocument]
  );

  useEffect(() => {
    (async () => {
      try {
        const [comps, projs, exs] = await Promise.all([
          api.listComponents(),
          api.listProjects(),
          api.listExamples(),
        ]);
        setCatalog(comps);
        setProjects(projs);
        setExamples(exs);
        setStatus('后端已连接');
        // best-effort device enumeration (needs rt_host built)
        api
          .listDevices()
          .then((d) => {
            const opts = [{ value: '', label: '默认设备' }];
            for (const x of d.capture || []) {
              opts.push({ value: x.name, label: `采集：${x.name}` });
            }
            for (const x of d.playback || []) {
              opts.push({ value: x.name, label: `播放：${x.name}` });
            }
            setDeviceOptions(opts);
          })
          .catch(() => {});
        if (projs.length > 0) {
          const document = await api.getProject(projs[0].name);
          loadDocument(projs[0].name, document, comps);
          setStatus(`已打开工程 ${projs[0].name}`);
        }
      } catch (e) {
        setStatus(`后端连接失败: ${api.errorDetail(e)}（请先运行 orpheus-cli serve）`);
      }
    })();
  }, [loadDocument]);

  // poll realtime session status (logs + probe values) while it is running
  useEffect(() => {
    if (!rt.running || !current) return undefined;
    const timer = setInterval(async () => {
      try {
        const s = await api.rtStatus(current);
        // process just died while we thought it was running: surface the reason
        if (!s.running && rtRef.current.running) {
          setLogCollapsed(false);
          setLog({
            title: s.exit_code === 0 ? '实时会话已结束' : `实时进程异常退出 (code ${s.exit_code})`,
            lines: (s.logs || []).slice(-20),
          });
          setStatus(
            s.exit_code === 0 ? '实时会话已结束' : `实时进程异常退出 (code ${s.exit_code})，原因见日志`
          );
          // 离线实时播放结束后刷新产物列表（wav_out 在宿主退出时落盘）
          api
            .listProjectFiles(current)
            .then((files) => {
              const outs = (files || []).filter((f) => (f.path || '').startsWith('outputs/'));
              if (outs.length) setOutputs(outs.map((f) => f.path));
            })
            .catch(() => {});
        }
        setRt({ running: s.running, logs: s.logs || [], probes: s.probes || {} });
        if (Object.keys(s.probes || {}).length > 0) {
          setViews((prev) => {
            const next = {};
            for (const [key, v] of Object.entries(prev)) {
              next[key] = {
                ...v,
                nodes: v.nodes.map((nd) =>
                  s.probes[nd.id] ? { ...nd, data: { ...nd.data, probe: s.probes[nd.id] } } : nd
                ),
              };
            }
            return next;
          });
        }
      } catch {
        /* ignore transient poll errors */
      }
    }, 250);
    return () => clearInterval(timer);
  }, [rt.running, current]);

  // keep sub-instance node ports in sync with subcomponent definitions
  useEffect(() => {
    setViews((prev) => {
      const next = {};
      for (const [key, v] of Object.entries(prev)) {
        next[key] = {
          ...v,
          nodes: v.nodes.map((n) => {
            if (!isSubRef(n.data.component)) return n;
            const sub = subsMeta.find((s) => s.id === subIdOf(n.data.component));
            if (!sub) return n;
            return {
              ...n,
              data: {
                ...n.data,
                ports: sub.ports.map((p) => ({ id: p.id, direction: p.direction })),
              },
            };
          }),
        };
      }
      return next;
    });
  }, [subsMeta]);

  // ---------------------------------------------------------- save

  const doSave = useCallback(async () => {
    if (!current || !doc) return;
    setSaving(true);
    try {
      const document = viewsToDoc(views, subsMeta, doc);
      await api.saveProject(current, document);
      setDoc(document);
      setDirty(false);
      setStatus(`已保存 ${current} · ${new Date().toLocaleTimeString()}`);
    } catch (e) {
      setStatus(`保存失败: ${api.errorDetail(e)}`);
    } finally {
      setSaving(false);
    }
  }, [current, doc, views, subsMeta]);

  // debounced auto-save: fire 1.5s after the last edit while dirty
  useEffect(() => {
    if (!dirty || !autoSave || !current) return undefined;
    const timer = setTimeout(doSave, AUTOSAVE_DELAY_MS);
    return () => clearTimeout(timer);
  }, [dirty, autoSave, current, views, subsMeta, doSave]);

  // Ctrl+S
  useEffect(() => {
    const onKey = (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 's') {
        e.preventDefault();
        doSave();
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [doSave]);

  // ---------------------------------------------------------- editing

  const onNodesChange = useCallback(
    (changes) => {
      const removed = changes.filter((c) => c.type === 'remove').map((c) => c.id);
      updateView(activeView, (v) => {
        const gone = new Set(removed);
        return {
          nodes: applyNodeChanges(changes, v.nodes),
          edges: gone.size
            ? v.edges.filter((e) => !gone.has(e.source) && !gone.has(e.target))
            : v.edges,
        };
      });
      if (removed.length) {
        setSelectedIds((ids) => ids.filter((id) => !removed.includes(id)));
        if (removed.includes(selectedId)) setSelectedId(null);
        setDirty(true);
      }
      if (changes.some((c) => ['position', 'add'].includes(c.type))) setDirty(true);
    },
    [activeView, selectedId, updateView]
  );

  /** 显式删除选中节点（多选/单选通用；连边一并清理）。 */
  const deleteSelected = useCallback(() => {
    const ids = selectedIds.filter((id) => view.nodes.some((n) => n.id === id));
    if (!ids.length) return;
    updateView(activeView, (v) => ({
      nodes: v.nodes.filter((n) => !ids.includes(n.id)),
      edges: v.edges.filter((e) => !ids.includes(e.source) && !ids.includes(e.target)),
    }));
    setSelectedIds([]);
    setSelectedId(null);
    setDirty(true);
  }, [selectedIds, view, activeView, updateView]);

  const onEdgesChange = useCallback(
    (changes) => {
      updateView(activeView, (v) => ({ ...v, edges: applyEdgeChanges(changes, v.edges) }));
      if (changes.some((c) => ['remove', 'add'].includes(c.type))) setDirty(true);
    },
    [activeView, updateView]
  );

  const onConnect = useCallback(
    (params) => {
      // an input pin may be driven by exactly one wire
      const occupied = view.edges.some(
        (e) => e.target === params.target && e.targetHandle === params.targetHandle
      );
      if (occupied) {
        setStatus(`输入引脚 ${params.target}:${params.targetHandle} 已有连线（先删除原连线）`);
        return;
      }
      updateView(activeView, (v) => ({ ...v, edges: addEdge(params, v.edges) }));
      setDirty(true);
    },
    [activeView, updateView, view.edges]
  );

  const onDragOver = useCallback((event) => {
    event.preventDefault();
    event.dataTransfer.dropEffect = 'move';
  }, []);

  const onDrop = useCallback(
    (event) => {
      event.preventDefault();
      const componentId = event.dataTransfer.getData('application/orpheus-component');
      const comp = catalogById[componentId];
      if (!comp) return;
      if (componentId === activeView) {
        setStatus('子组件不能包含自身');
        return;
      }
      const short = isSubRef(componentId) ? subIdOf(componentId) : componentId.split('.').pop();
      const id = uniqueNodeId(view.nodes, short);
      const params = defaultParams(comp);
      const position = screenToFlowPosition({ x: event.clientX, y: event.clientY });
      updateView(activeView, (v) => ({
        ...v,
        nodes: [
          ...v.nodes,
          {
            id,
            type: 'orpheus',
            position,
            data: {
              label: id,
              component: componentId,
              params,
              clockSource: !!comp.clock_source,
              ports: resolvePorts(comp, params),
              parameters: comp.parameters || [],
            },
          },
        ],
      }));
      setSelectedId(id);
      setDirty(true);
    },
    [catalogById, activeView, view.nodes, screenToFlowPosition, updateView]
  );

  const onParamChange = useCallback(
    (paramId, value) => {
      // live-push to the running realtime session (unless recompile is required)
      if (rtRef.current.running && current && selectedId) {
        const node = view.nodes.find((nd) => nd.id === selectedId);
        const schema = (node?.data.parameters || []).find((p) => p.id === paramId);
        if (schema && schema.update_policy !== 'restart_required') {
          api.rtSetParam(current, selectedId, paramId, value).catch(() => {});
        } else if (schema) {
          setStatus(`参数 ${paramId} 需重新编译，实时会话中不生效`);
        }
      }
      updateView(activeView, (v) => {
        const nodes = v.nodes.map((nd) => {
          if (nd.id !== selectedId) return nd;
          const params = { ...nd.data.params, [paramId]: value };
          // recompute pins: signature-affecting params (e.g. channels) change port count
          const comp = catalogById[nd.data.component];
          const ports = comp ? resolvePorts(comp, params) : nd.data.ports;
          return { ...nd, data: { ...nd.data, params, ports } };
        });
        // prune edges whose handle no longer exists on the edited node
        const edited = nodes.find((n) => n.id === selectedId);
        const valid = new Set((edited?.data.ports || []).map((p) => p.id));
        const edges = v.edges.filter(
          (e) =>
            !(e.source === selectedId && !valid.has(e.sourceHandle)) &&
            !(e.target === selectedId && !valid.has(e.targetHandle))
        );
        return { nodes, edges };
      });
      setDirty(true);
    },
    [activeView, selectedId, updateView, catalogById]
  );

  /** 参数面板专用：更新任意视图（主图/子组件标签）里某节点的参数，并在实时会话中即时推送。 */
  const onNodeParamChange = useCallback(
    (viewKey, nodeId, paramId, value, flatId) => {
      const nd = (views[viewKey] && views[viewKey].nodes.find((n) => n.id === nodeId)) || null;
      const schema = (nd && nd.data.parameters || []).find((p) => p.id === paramId);
      if (rtRef.current.running && current && schema) {
        if (schema.update_policy !== 'restart_required') {
          api.rtSetParam(current, flatId || nodeId, paramId, value).catch(() => {});
        } else {
          setStatus(`参数 ${paramId} 需重新编译，实时会话中不生效`);
        }
      }
      updateView(viewKey, (v) => {
        const nodes = v.nodes.map((n) => {
          if (n.id !== nodeId) return n;
          const params = { ...n.data.params, [paramId]: value };
          const comp = catalogById[n.data.component];
          const ports = comp ? resolvePorts(comp, params) : n.data.ports;
          return { ...n, data: { ...n.data, params, ports } };
        });
        const edited = nodes.find((n) => n.id === nodeId);
        const valid = new Set((edited && edited.data.ports || []).map((p) => p.id));
        const edges = v.edges.filter(
          (e) =>
            !(e.source === nodeId && !valid.has(e.sourceHandle)) &&
            !(e.target === nodeId && !valid.has(e.targetHandle))
        );
        return { nodes, edges };
      });
      setDirty(true);
    },
    [views, current, updateView, catalogById]
  );

  /** 参数面板导入：把值写回各视图节点参数（含 Bulk 数组回写字符串），并在实时会话中推送非重启参数。 */
  const onImportApply = useCallback(
    (items) => {
      if (!items.length) return;
      setViews((prev) => {
        const next = {};
        for (const [key, v] of Object.entries(prev)) {
          next[key] = {
            ...v,
            nodes: v.nodes.map((nd) => {
              const item = items.find((it) => it.viewKey === key && it.nodeId === nd.id);
              if (!item) return nd;
              const params = { ...nd.data.params };
              for (const [k, val] of Object.entries(item.values || {})) params[k] = val;
              for (const [k, arr] of Object.entries(item.bulk || {})) {
                params[k] = Array.isArray(arr) ? arr.join(', ') : String(arr);
              }
              const comp = catalogById[nd.data.component];
              return {
                ...nd,
                data: { ...nd.data, params, ports: comp ? resolvePorts(comp, params) : nd.data.ports },
              };
            }),
          };
        }
        return next;
      });
      setDirty(true);
      if (rtRef.current.running && current) {
        for (const item of items) {
          for (const [k, val] of Object.entries(item.values || {})) {
            const nd = (views[item.viewKey] && views[item.viewKey].nodes.find((n) => n.id === item.nodeId)) || null;
            const schema = (nd && nd.data.parameters || []).find((p) => p.id === k);
            if (schema && schema.update_policy !== 'restart_required') {
              api.rtSetParam(current, item.flatId, k, val).catch(() => {});
            }
          }
        }
      }
      setStatus(`已导入 ${items.length} 个节点的参数（含 Bulk 数据）`);
    },
    [views, current, catalogById]
  );

  const onLocateParam = useCallback(
    (viewKey, nodeId) => {
      setActiveView(viewKey);
      setSelectedId(nodeId);
      setShowParams(false);
    },
    []
  );

  /** 参数面板：把数值数组写入实时会话的 BULK 槽（如 biquad_bank 系数）。 */
  const onWriteBulk = useCallback(
    async (flatId, key, values) => {
      if (!current || !rtRef.current.running) {
        setStatus('实时会话未运行，无法写入 Bulk 槽');
        return;
      }
      try {
        await api.rtWriteBulk(current, flatId, key, values);
        setStatus(`Bulk ${key} 已写入 ${flatId}（${values.length} 个值）`);
      } catch (e) {
        setStatus(`Bulk 写入失败: ${api.errorDetail(e)}`);
      }
    },
    [current]
  );

  /** 参数面板：按 32 位数据 ID 查询内存（内存透明），需实时会话运行。 */
  const onResolve = useCallback(
    async (nodeId, id) => {
      if (!current || !rtRef.current.running) {
        setStatus('实时会话未运行，无法解析地址');
        return;
      }
      try {
        const r = await api.rtResolve(current, id);
        setStatus(
          `ID 0x${id.toString(16).toUpperCase()} → ${r.node || ''} ${r.key || ''}：` +
            `${r.kind}/${r.form}，count=${r.count}，${r.byte_size} B，` +
            `base=${r.base} offset=${r.offset}`
        );
      } catch (e) {
        setStatus(`解析失败: ${api.errorDetail(e)}`);
      }
    },
    [current]
  );

  /** 参数面板：按 ID 读回 BULK active bank（高速大块读回）。 */
  const onReadBulk = useCallback(
    async (nodeId, id) => {
      if (!current || !rtRef.current.running) {
        setStatus('实时会话未运行，无法读回 Bulk');
        return;
      }
      try {
        const r = await api.rtReadBulkById(current, id);
        const vals = r.values || [];
        setStatus(
          `Bulk 0x${id.toString(16).toUpperCase()} 读回 ${vals.length} 个值：` +
            vals.slice(0, 8).join(', ') +
            (vals.length > 8 ? ', …' : '')
        );
      } catch (e) {
        setStatus(`Bulk 读回失败: ${api.errorDetail(e)}`);
      }
    },
    [current]
  );

  /** 预设保存在工程文档顶层 `presets` 字段，随正常保存流程持久化。 */
  const onSavePreset = useCallback((name, snapshot) => {
    setDoc((d) => {
      const base = d || {};
      const list = [...(base.presets || []).filter((x) => x.name !== name)];
      return { ...base, presets: [...list, { ...snapshot, name }] };
    });
    setDirty(true);
    setStatus(`已保存预设 ${name}`);
  }, []);

  const onDeletePreset = useCallback((name) => {
    setDoc((d) => {
      const base = d || {};
      return { ...base, presets: (base.presets || []).filter((x) => x.name !== name) };
    });
    setDirty(true);
    setStatus(`已删除预设 ${name}`);
  }, []);

  const onDeleteNode = useCallback(
    (nodeId) => {
      updateView(activeView, (v) => ({
        nodes: v.nodes.filter((nd) => nd.id !== nodeId),
        edges: v.edges.filter((e) => e.source !== nodeId && e.target !== nodeId),
      }));
      setSelectedId(null);
      setDirty(true);
    },
    [activeView, updateView]
  );

  const onNodeDoubleClick = useCallback(
    (_, node) => {
      if (!isSubRef(node.data.component)) return;
      const key = node.data.component;
      setOpenTabs((tabs) => (tabs.includes(key) ? tabs : [...tabs, key]));
      setActiveView(key);
      setSelectedId(null);
    },
    []
  );

  const closeTab = useCallback(
    (key) => {
      setOpenTabs((tabs) => tabs.filter((t) => t !== key));
      if (activeView === key) setActiveView('main');
    },
    [activeView]
  );

  // ---------------------------------------------------------- subcomponents

  const promptSubId = useCallback(() => {
    const name = window.prompt('子组件 id（字母/数字/-/_）:');
    if (!name) return null;
    const id = name.trim();
    if (!/^[A-Za-z0-9_-]+$/.test(id)) {
      setStatus(`非法子组件 id: ${id}`);
      return null;
    }
    if (subsMeta.some((s) => s.id === id)) {
      setStatus(`子组件已存在: ${id}`);
      return null;
    }
    return id;
  }, [subsMeta]);

  const createSub = useCallback(() => {
    const id = promptSubId();
    if (!id) return;
    setSubsMeta((prev) => [...prev, { id, name: id, description: '', ports: [] }]);
    setViews((prev) => ({ ...prev, [subViewKey(id)]: EMPTY_VIEW }));
    setOpenTabs((tabs) => [...tabs, subViewKey(id)]);
    setActiveView(subViewKey(id));
    setDirty(true);
  }, [promptSubId]);

  const wrapSelection = useCallback(() => {
    const S = new Set(selectedIds.filter((id) => view.nodes.some((n) => n.id === id)));
    if (S.size === 0) {
      setStatus('请先在当前视图中选择要包装的节点');
      return;
    }
    const subId = promptSubId();
    if (!subId) return;

    const internalNodes = view.nodes.filter((n) => S.has(n.id));
    const internalEdges = view.edges.filter((e) => S.has(e.source) && S.has(e.target));
    const boundaryIn = view.edges.filter((e) => !S.has(e.source) && S.has(e.target));
    const boundaryOut = view.edges.filter((e) => S.has(e.source) && !S.has(e.target));

    const ports = [];
    const inPortOf = {};
    for (const e of boundaryIn) {
      const key = `${e.target}:${e.targetHandle}`;
      if (!inPortOf[key]) {
        const pid = `in${Object.keys(inPortOf).length + 1}`;
        inPortOf[key] = pid;
        ports.push({ id: pid, direction: 'input', maps_to: key });
      }
    }
    const outPortOf = {};
    for (const e of boundaryOut) {
      const key = `${e.source}:${e.sourceHandle}`;
      if (!outPortOf[key]) {
        const pid = `out${Object.keys(outPortOf).length + 1}`;
        outPortOf[key] = pid;
        ports.push({ id: pid, direction: 'output', maps_to: key });
      }
    }

    const cx = internalNodes.reduce((s, n) => s + n.position.x, 0) / internalNodes.length;
    const cy = internalNodes.reduce((s, n) => s + n.position.y, 0) / internalNodes.length;
    const instanceId = uniqueNodeId(
      view.nodes.filter((n) => !S.has(n.id)),
      subId
    );

    setSubsMeta((prev) => [...prev, { id: subId, name: subId, description: '', ports }]);
    setViews((prev) => ({
      ...prev,
      [activeView]: {
        nodes: [
          ...prev[activeView].nodes.filter((n) => !S.has(n.id)),
          {
            id: instanceId,
            type: 'orpheus',
            position: { x: cx, y: cy },
            data: {
              label: instanceId,
              component: subViewKey(subId),
              params: {},
              ports: ports.map((p) => ({ id: p.id, direction: p.direction })),
              parameters: [],
            },
          },
        ],
        edges: [
          ...prev[activeView].edges.filter((e) => !S.has(e.source) && !S.has(e.target)),
          ...boundaryIn.map((e) => ({
            ...e,
            id: `e-${e.source}:${e.sourceHandle}-${instanceId}:${inPortOf[`${e.target}:${e.targetHandle}`]}`,
            target: instanceId,
            targetHandle: inPortOf[`${e.target}:${e.targetHandle}`],
          })),
          ...boundaryOut.map((e) => ({
            ...e,
            id: `e-${instanceId}:${outPortOf[`${e.source}:${e.sourceHandle}`]}-${e.target}:${e.targetHandle}`,
            source: instanceId,
            sourceHandle: outPortOf[`${e.source}:${e.sourceHandle}`],
          })),
        ],
      },
      [subViewKey(subId)]: { nodes: internalNodes, edges: internalEdges },
    }));
    setOpenTabs((tabs) => [...tabs, subViewKey(subId)]);
    setActiveView(subViewKey(subId));
    setSelectedIds([]);
    setSelectedId(null);
    setDirty(true);
    setStatus(`已包装为子组件 ${subId}（${ports.length} 个端口）`);
  }, [selectedIds, view, activeView, promptSubId]);

  const deleteSub = useCallback(
    (subComponentId) => {
      const id = subIdOf(subComponentId);
      const used = Object.values(views).some((v) =>
        v.nodes.some((n) => n.data.component === subComponentId)
      );
      if (used) {
        setStatus(`子组件 ${id} 仍被实例引用，无法删除`);
        return;
      }
      if (!window.confirm(`确认删除工程子组件「${id}」？此操作不可撤销。`)) return;
      setSubsMeta((prev) => prev.filter((s) => s.id !== id));
      setViews((prev) => {
        const next = { ...prev };
        delete next[subComponentId];
        return next;
      });
      closeTab(subComponentId);
      setDirty(true);
    },
    [views, closeTab]
  );

  /** 重命名画布节点（仅显示名；节点 id/协议地址不变）。 */
  const onRenameNode = useCallback(
    (nodeId) => {
      const nd = view.nodes.find((n) => n.id === nodeId);
      if (!nd) return;
      const name = window.prompt('节点名称（用于画布/参数面板定位；不影响节点 id 与协议地址）:', nd.data.label || nd.id);
      if (name === null || !name.trim()) return;
      updateView(activeView, (v) => ({
        ...v,
        nodes: v.nodes.map((n) =>
          n.id === nodeId ? { ...n, data: { ...n.data, label: name.trim() } } : n
        ),
      }));
      setDirty(true);
    },
    [activeView, view, updateView]
  );

  /** 自定义组件管理：删除（确认） / 提升为公共库（之后不可直接删除）。 */
  const onDeleteComponent = useCallback(
    async (componentId) => {
      if (!window.confirm(`确认删除自定义组件「${componentId}」？将移除其源码目录，此操作不可撤销。`)) return;
      try {
        await api.deleteComponent(componentId);
        setCatalog(await api.listComponents());
        setStatus(`已删除自定义组件 ${componentId}`);
      } catch (e) {
        setStatus(`删除组件失败: ${api.errorDetail(e)}`);
      }
    },
    []
  );

  const onPromoteComponent = useCallback(
    async (componentId) => {
      if (!window.confirm(`确认把「${componentId}」提升为公共库组件？提升后不能再从界面删除。`)) return;
      try {
        await api.promoteComponent(componentId);
        setCatalog(await api.listComponents());
        setStatus(`已提升为公共库组件 ${componentId}`);
      } catch (e) {
        setStatus(`提升组件失败: ${api.errorDetail(e)}`);
      }
    },
    []
  );

  const addSubPort = useCallback(
    (direction, mapsTo) => {
      if (!activeSub) return;
      const prefix = direction === 'input' ? 'in' : 'out';
      let i = 0;
      let pid;
      do {
        pid = `${prefix}${++i}`;
      } while (activeSub.ports.some((p) => p.id === pid));
      setSubsMeta((prev) =>
        prev.map((s) =>
          s.id === activeSub.id ? { ...s, ports: [...s.ports, { id: pid, direction, maps_to: mapsTo }] } : s
        )
      );
      setDirty(true);
    },
    [activeSub]
  );

  const removeSubPort = useCallback(
    (portId) => {
      if (!activeSub) return;
      const key = subViewKey(activeSub.id);
      const used = Object.entries(views).some(
        ([vk, v]) =>
          vk !== key &&
          v.edges.some((e) => {
            const srcNode = v.nodes.find((n) => n.id === e.source);
            const dstNode = v.nodes.find((n) => n.id === e.target);
            return (
              (srcNode?.data.component === key && e.sourceHandle === portId) ||
              (dstNode?.data.component === key && e.targetHandle === portId)
            );
          })
      );
      if (used) {
        setStatus(`端口 ${portId} 仍被实例连接引用，无法删除`);
        return;
      }
      setSubsMeta((prev) =>
        prev.map((s) =>
          s.id === activeSub.id ? { ...s, ports: s.ports.filter((p) => p.id !== portId) } : s
        )
      );
      setDirty(true);
    },
    [activeSub, views]
  );

  // ---------------------------------------------------------- actions

  const ensureSaved = useCallback(async () => {
    if (dirtyRef.current) await doSave();
  }, [doSave]);

  const doCompile = useCallback(async () => {
    if (!current) return;
    await ensureSaved();
    try {
      const r = await api.compileProject(current);
      setStatus(`编译成功: ${r.nodes} 节点, ${r.buffers} buffers`);
      setLog({ title: '编译结果', lines: [`执行顺序: ${r.execution_order.join(' → ')}`, r.plan_path] });
      // annotate nodes with compiled rate info (time-tree badges)
      if (r.node_rates) {
        setViews((prev) => {
          const next = {};
          for (const [key, v] of Object.entries(prev)) {
            next[key] = {
              ...v,
              nodes: v.nodes.map((nd) =>
                r.node_rates[nd.id]
                  ? { ...nd, data: { ...nd.data, rate: r.node_rates[nd.id] } }
                  : nd
              ),
            };
          }
          return next;
        });
      }
      if (r.id_map) setIdMap(r.id_map);
    } catch (e) {
      setStatus('编译失败');
      setLog({ title: '编译错误', lines: [api.errorDetail(e)] });
    }
  }, [current, ensureSaved]);

  const doRun = useCallback(async () => {
    if (!current) return;
    await ensureSaved();
    setStatus('运行中…');
    try {
      const r = await api.runProject(current, paceRun);
      if (r.mode === 'realtime' || r.mode === 'offline_live') {
        // device graph: base host started a realtime session
        setRt({ running: true, logs: [], probes: {} });
        setStatus(
          r.mode === 'offline_live'
            ? '离线实时播放中（按真实时长，可观察进度/曲线）'
            : '实时运行中（含设备组件，调参数即时生效）'
        );
        return;
      }
      setStatus(r.status === 'ok' ? '运行成功' : `运行失败 (exit ${r.returncode})`);
      setOutputs(r.outputs || []);
      // inject probe readback values into node bodies (e.g. level meters)
      if (r.probes?.length) {
        const byNode = {};
        for (const p of r.probes) {
          (byNode[p.node] = byNode[p.node] || {})[p.param] = p.value;
        }
        setViews((prev) => {
          const next = {};
          for (const [key, v] of Object.entries(prev)) {
            next[key] = {
              ...v,
              nodes: v.nodes.map((nd) =>
                byNode[nd.id] ? { ...nd, data: { ...nd.data, probe: byNode[nd.id] } } : nd
              ),
            };
          }
          return next;
        });
      }
      setLog({
        title: '运行输出',
        lines: [
          r.built_components?.length ? `新构建组件: ${r.built_components.join(', ')}` : null,
          r.stdout,
          r.stderr ? `stderr:\n${r.stderr}` : null,
        ].filter(Boolean),
      });
    } catch (e) {
      setStatus('运行失败');
      setLog({ title: '运行错误', lines: [api.errorDetail(e)] });
    }
  }, [current, ensureSaved, paceRun]);

  const doRunGenerated = useCallback(async () => {
    if (!current) return;
    await ensureSaved();
    setStatus('生成代码并构建中…');
    try {
      const r = await api.runGenerated(current);
      setStatus(
        r.status === 'ok' ? `编译后运行成功（${r.blocks} 块）` : `编译后运行失败 (exit ${r.returncode})`
      );
      setOutputs(r.outputs || []);
      if (r.generated_path) {
        setGeneratedInfo({
          path: r.generated_path,
          url: api.downloadGeneratedUrl(current),
        });
      }
      setLog({
        title: '编译后运行输出',
        lines: [r.stdout, r.stderr ? `stderr:\n${r.stderr}` : null].filter(Boolean),
      });
    } catch (e) {
      setStatus('编译后运行失败');
      setLog({ title: '编译后运行错误', lines: [api.errorDetail(e)] });
    }
  }, [current, ensureSaved]);

  const doGenerate = useCallback(async () => {
    if (!current) return;
    await ensureSaved();
    setStatus('生成独立 C 工程中…');
    try {
      const r = await api.generateProject(current);
      setStatus(`已生成独立 C 工程：${r.generated_dir}`);
      setGeneratedInfo({
        path: r.generated_path,
        url: api.downloadGeneratedUrl(current),
      });
      setLog({
        title: '生成结果',
        lines: [
          `生成目录（工程目录下）: ${r.generated_path}`,
          '嵌入部署：改 src/platform_io.c 的 USER CODE 段接入实际 DMA/编解码器后，按目标工具链交叉编译。',
          `PC 冒烟运行默认块数: ${r.blocks_default}`,
        ],
      });
    } catch (e) {
      setStatus(`生成失败: ${api.errorDetail(e)}`);
      setLog({ title: '生成错误', lines: [api.errorDetail(e)] });
    }
  }, [current, ensureSaved]);

  const doRtStop = useCallback(async () => {
    if (!current) return;
    try {
      await api.rtStop(current);
      const s = await api.rtStatus(current);
      setRt({ running: false, logs: s.logs || [], probes: s.probes || {} });
      setStatus('实时会话已停止');
    } catch (e) {
      setStatus(`停止失败: ${api.errorDetail(e)}`);
    }
  }, [current]);

  const doCreate = useCallback(async () => {
    const name = window.prompt('新工程名称（字母/数字/-/_）:');
    if (!name) return;
    try {
      const r = await api.createProject(name);
      await refreshProjects();
      openProject(name, r.document);
    } catch (e) {
      setStatus(`创建失败: ${api.errorDetail(e)}`);
    }
  }, [refreshProjects, openProject]);

  const doImportExample = useCallback(
    async (example) => {
      if (!example) return;
      const name = window.prompt('导入为工程名称:', example);
      if (!name) return;
      try {
        const r = await api.createProject(name, example);
        await refreshProjects();
        openProject(name, r.document);
      } catch (e) {
        setStatus(`导入失败: ${api.errorDetail(e)}`);
      }
    },
    [refreshProjects, openProject]
  );

  const onDistillFile = useCallback(
    async (e) => {
      const f = e.target.files && e.target.files[0];
      e.target.value = '';
      if (!f) return;
      const text = await f.text();
      const suggested = f.name
        .replace(/\.[^.]+$/, '')
        .replace(/[^A-Za-z0-9_-]+/g, '_')
        .replace(/^_+|_+$/g, '');
      const name = window.prompt('导入为新工程名称:', suggested || 'distilled_model');
      if (!name) return;
      try {
        const r = await api.importDistilled(name, text);
        await refreshProjects();
        openProject(name, r.document);
        setStatus(`已导入蒸馏模型 ${name}`);
      } catch (err) {
        setStatus(`导入失败: ${api.errorDetail(err)}`);
      }
    },
    [refreshProjects, openProject]
  );

  // ---------------------------------------------------------- render

  const selectedNode = view.nodes.find((nd) => nd.id === selectedId) || null;

  // 点击工具栏外关闭展开菜单
  useEffect(() => {
    if (!openMenu) return undefined;
    const onDocClick = (e) => {
      if (!e.target.closest('.toolbar')) setOpenMenu(null);
    };
    document.addEventListener('click', onDocClick);
    return () => document.removeEventListener('click', onDocClick);
  }, [openMenu]);

  return (
    <div className="app">
      <div className="toolbar">
        <h1>Orpheus</h1>
        <span className="tb-group">
          <select value={current || ''} onChange={(e) => e.target.value && openProject(e.target.value)}>
            <option value="" disabled>
              {projects.length ? '选择工程…' : '无工程'}
            </option>
            {projects.map((p) => (
              <option key={p.name} value={p.name}>
                {p.name}
              </option>
            ))}
          </select>
          <button onClick={doCreate}>新建工程</button>
          <select value="" onChange={(e) => doImportExample(e.target.value)}>
            <option value="">导入示例…</option>
            {examples.map((ex) => (
              <option key={ex} value={ex}>
                {ex}
              </option>
            ))}
          </select>
          <button
            className="tb-more"
            onClick={() => setOpenMenu(openMenu === 'project' ? null : 'project')}
            title="更多工程操作"
          >
            ⋯
          </button>
          {openMenu === 'project' && (
            <div className="tb-menu">
              <button
                onClick={() => {
                  setOpenMenu(null);
                  distillFileRef.current && distillFileRef.current.click();
                }}
              >
                ⤵ 导入模型
              </button>
              <input
                ref={distillFileRef}
                type="file"
                accept=".yaml,.yml,.json"
                style={{ display: 'none' }}
                onChange={onDistillFile}
              />
            </div>
          )}
        </span>
        <span className="toolbar-sep" />
        <span className="tb-group">
          <span title="左键拖拽平移画布；Ctrl+拖拽圈选；Ctrl+点击多选">
            <button onClick={wrapSelection} disabled={!current || selectedIds.length === 0}>
              包装为子组件
            </button>
          </span>
          <button onClick={createSub} disabled={!current}>
            新建子组件
          </button>
          <button
            className="danger-soft"
            onClick={deleteSelected}
            disabled={!selectedIds.length}
            title="删除选中的节点（Delete 键同样可用）"
          >
            删除选中
          </button>
        </span>
        <span className="toolbar-sep" />
        <span className="tb-group">
          <button onClick={doSave} disabled={!dirty || !current || saving}>
            {saving ? '保存中…' : '保存'}
          </button>
          <label className="autosave">
            <input type="checkbox" checked={autoSave} onChange={(e) => setAutoSave(e.target.checked)} />
            自动保存
          </label>
        </span>
        <span className="toolbar-sep" />
        <span className="tb-group">
          <button onClick={() => setShowSettings(true)} disabled={!current || !doc} title="工程全局设置（采样率/块长度/缓冲）">
            ⚙ 设置
          </button>
          <button
            onClick={() => setShowParams(true)}
            disabled={!current || !doc}
            title="按树形浏览全部参数与探针（按数据类型分类），支持导入导出（含 Bulk 数据）"
          >
            ☰ 参数面板
          </button>
        </span>
        <span className="toolbar-sep" />
        <span className="tb-group">
          <button className="primary" onClick={doRun} disabled={!current || rt.running}>
            ▶ 运行
          </button>
          {rt.running && (
            <button className="danger-tool" onClick={doRtStop}>
              ■ 停止
            </button>
          )}
          <label className="pace-label" title="离线运行时按真实时长播放（进度条/曲线实时走动）">
            <input
              type="checkbox"
              checked={paceRun}
              onChange={(e) => setPaceRun(e.target.checked)}
              disabled={!current || rt.running}
            />
            真实时长
          </label>
          <button
            className="tb-more"
            onClick={() => setOpenMenu(openMenu === 'run' ? null : 'run')}
            title="更多运行/生成操作"
          >
            ⋯
          </button>
          {openMenu === 'run' && (
            <div className="tb-menu">
              <button
                onClick={() => {
                  setOpenMenu(null);
                  doCompile();
                }}
              >
                编译
              </button>
              <button
                onClick={() => {
                  setOpenMenu(null);
                  doRunGenerated();
                }}
              >
                ⚙ 编译后运行
              </button>
              <button
                onClick={() => {
                  setOpenMenu(null);
                  doGenerate();
                }}
              >
                ⤓ 生成代码
              </button>
              {current && (
                <a className="button" href={api.downloadUrl(current)} download onClick={() => setOpenMenu(null)}>
                  下载 zip
                </a>
              )}
            </div>
          )}
        </span>
      </div>
      <div className="statusbar">
        <span className={`status ${dirty ? 'dirty' : ''}`}>
          {rt.running ? '⏺ 实时运行中' : dirty ? '● 未保存' : status}
        </span>
      </div>
      <div className="tabbar">
        {openTabs.map((key) => (
          <div
            key={key}
            className={`tab ${activeView === key ? 'active' : ''}`}
            onClick={() => {
              setActiveView(key);
              setSelectedId(null);
            }}
          >
            {key === 'main' ? '主图' : `📦 ${subsMeta.find((s) => subViewKey(s.id) === key)?.name || key}`}
            {key !== 'main' && (
              <span
                className="tab-close"
                onClick={(e) => {
                  e.stopPropagation();
                  closeTab(key);
                }}
              >
                ×
              </span>
            )}
          </div>
        ))}
      </div>
      <div className="main">
        <button
          className="side-toggle side-toggle-left"
          onClick={() => setLeftOpen((v) => !v)}
          title={leftOpen ? '收起组件库' : '展开组件库'}
        >
          {leftOpen ? '«' : '»'}
        </button>
        {leftOpen && (
          <div className="side-panel side-panel-left">
            <div className="side-panel-header">
              <span>组件库</span>
              <button className="side-collapse" onClick={() => setLeftOpen(false)} title="收起组件库">
                « 收起
              </button>
            </div>
            <Palette
              components={catalog}
              subsMeta={subsMeta}
              onDeleteSub={deleteSub}
              onDeleteComponent={onDeleteComponent}
              onPromoteComponent={onPromoteComponent}
            />
          </div>
        )}
        <div className="canvas" onDrop={onDrop} onDragOver={onDragOver}>
          <ReactFlow
            nodes={view.nodes}
            edges={view.edges}
            onNodesChange={onNodesChange}
            onEdgesChange={onEdgesChange}
            onConnect={onConnect}
            onNodeClick={(_, node) => setSelectedId(node.id)}
            onNodeDoubleClick={onNodeDoubleClick}
            onPaneClick={() => setSelectedId(null)}
            onSelectionChange={({ nodes: sn }) => {
              const ids = (sn || []).map((n) => n.id);
              setSelectedIds(ids);
              // 视觉选中同步参数面板：某些内部元素（如监控节点 ⤢）会吞掉 click，
              // 但 React Flow 的视觉选中仍发生——用 selection 保证 selectedId 一致。
              if (ids.length === 1) setSelectedId(ids[0]);
              else if (ids.length === 0) setSelectedId(null);
            }}
            nodeTypes={nodeTypes}
            deleteKeyCode={['Delete', 'Backspace']}
            selectionKeyCode="Control"
            multiSelectionKeyCode="Control"
            fitView
          >
            <Background />
            <Controls />
            <MiniMap />
          </ReactFlow>
        </div>
        {rightOpen && (
          <div className="rightbar">
            <div className="side-panel-header">
              <span>参数 / 子组件</span>
              <button className="side-collapse" onClick={() => setRightOpen(false)} title="收起参数面板">
                收起 »
              </button>
            </div>
            {activeSub && (
              <SubPortsPanel
                sub={activeSub}
                viewNodes={view.nodes}
                catalogById={catalogById}
                onAddPort={addSubPort}
                onRemovePort={removeSubPort}
              />
            )}
            <ParamPanel
              node={selectedNode}
              onParamChange={onParamChange}
              onDeleteNode={onDeleteNode}
              onRenameNode={onRenameNode}
              ctx={paramCtx}
            />
          </div>
        )}
        <button
          className="side-toggle side-toggle-right"
          onClick={() => setRightOpen((v) => !v)}
          title={rightOpen ? '收起参数面板' : '展开参数面板'}
        >
          {rightOpen ? '»' : '«'}
        </button>
      </div>
      {showSettings && doc && (
        <ProjectSettings
          doc={doc}
          onClose={() => setShowSettings(false)}
          onSave={(fields) => {
            setDoc({ ...doc, ...fields });
            setDirty(true);
            setShowSettings(false);
          }}
        />
      )}
      {showParams && doc && (
        <ParamBrowser
          projectName={current}
          views={views}
          catalogById={catalogById}
          rt={rt}
          ctx={paramCtx}
          onNodeParamChange={onNodeParamChange}
          onImportApply={onImportApply}
          onWriteBulk={onWriteBulk}
          idMap={idMap}
          onResolve={onResolve}
          onReadBulk={onReadBulk}
          presets={doc.presets || []}
          onSavePreset={onSavePreset}
          onDeletePreset={onDeletePreset}
          onLocate={onLocateParam}
          onClose={() => setShowParams(false)}
        />
      )}
      {(log || outputs.length > 0 || generatedInfo || rt.logs.length > 0 || rt.running) && (
        <div className={`bottombar ${logCollapsed ? 'collapsed' : ''}`}>
          <div
            className="bottombar-header"
            onMouseDown={onLogDragStart}
            title="拖拽调整高度"
          >
            <span className="bottombar-title">日志与产物（拖拽此栏调整高度）</span>
            <button
              className="bottombar-toggle"
              onMouseDown={(e) => e.stopPropagation()}
              onClick={() => setLogCollapsed((c) => !c)}
            >
              {logCollapsed ? '展开 ▲' : '收起 ▼'}
            </button>
          </div>
          {!logCollapsed && (
            <div className="bottombar-body" style={{ height: logHeight }}>
              {(rt.running || rt.logs.length > 0) && (
                <div className="log">
                  <strong>实时日志</strong>
                  {(() => {
                    const rb = rt.probes && rt.probes["__host__"] && rt.probes["__host__"].rb;
                    if (!rb) return null;
                    if (!rb.bridge) {
                      return <div className="rb-gauge"><span className="rb-text">设备时钟模式（单一设备，无环形缓冲水位）</span></div>;
                    }
                    const pct = rb.capacity > 0 ? Math.round((rb.level / rb.capacity) * 100) : 0;
                    if (rb.primed === false) {
                      return (
                        <div className="rb-gauge" title="正在预充：播放暂输出静音，等待采集填满缓冲水位后再开始。持续不完成=采集未供数(loopback目标未播放/采集设备异常)。">
                          <span className="rb-label">预充中</span>
                          <div className="rb-bar"><div className="rb-fill" style={{ width: pct + "%", background: "#4dabf7" }} /></div>
                          <span className="rb-text">正在预充缓冲… {rb.level}/{rb.capacity} 帧 ({pct}%) · 等待采集供数</span>
                        </div>
                      );
                    }
                    const color = pct < 10 || pct > 90 ? "#e03131" : pct < 25 || pct > 75 ? "#f59f00" : "#2f9e44";
                    const warn = pct < 10 || pct > 90 ? "rb-alert" : "";
                    return (
                      <div className="rb-gauge" title="缓冲水位：持续偏低=采集供数不足(欠载)，持续偏高=采集过快(溢出)">
                        <span className="rb-label">缓冲水位</span>
                        <div className="rb-bar"><div className="rb-fill" style={{ width: pct + "%", background: color }} /></div>
                        <span className={"rb-text " + warn}>{rb.level}/{rb.capacity} 帧 ({pct}%) · 欠载 {rb.underruns} · 溢出 {rb.overruns}</span>
                      </div>
                    );
                  })()}
                  <pre>{rt.logs.slice(-100).join('\n') || '（等待日志…）'}</pre>
                </div>
              )}
              {log && (
                <div className="log">
                  <strong>{log.title}</strong>
                  {log.lines.map((line, i) => (
                    <pre key={i}>{line}</pre>
                  ))}
                </div>
              )}
              {outputs.length > 0 && (
                <div className="outputs">
                  <strong>产物</strong>
                  {outputs.map((o) => (
                    <div key={o} className="output-item">
                      <a href={api.projectFileUrl(current, o)} target="_blank" rel="noreferrer">
                        {o}
                      </a>
                      {o.endsWith('.wav') && <audio controls src={api.projectFileUrl(current, o)} />}
                    </div>
                  ))}
                </div>
              )}
              {generatedInfo && (
                <div className="outputs">
                  <strong>生成工程</strong>
                  <div className="output-item">
                    <span className="muted">{generatedInfo.path}（工程目录下）</span>
                    <a href={generatedInfo.url} download>
                      下载生成代码 (zip)
                    </a>
                  </div>
                </div>
              )}
            </div>
          )}
        </div>
      )}
    </div>
  );
}

export default function App() {
  return (
    <ReactFlowProvider>
      <Editor />
    </ReactFlowProvider>
  );
}
