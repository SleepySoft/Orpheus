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
  useKeyPress,
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
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const [autoSave, setAutoSave] = useState(true);
  const [showSettings, setShowSettings] = useState(false);
  const [status, setStatus] = useState('未连接后端');
  const [log, setLog] = useState(null);
  const [outputs, setOutputs] = useState([]);
  const [deviceOptions, setDeviceOptions] = useState([{ value: '', label: '默认设备' }]);
  const [rt, setRt] = useState({ running: false, logs: [], probes: {} });
  const [logCollapsed, setLogCollapsed] = useState(false);
  const [logHeight, setLogHeight] = useState(180);
const { screenToFlowPosition } = useReactFlow();
  // 按住空格 = 临时切到平移模式（Figma 式）：左键拖拽从"框选"变"平移"
  const spacePressed = useKeyPress('Space');

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
      updateView(activeView, (v) => ({ ...v, nodes: applyNodeChanges(changes, v.nodes) }));
      if (changes.some((c) => ['position', 'remove', 'add'].includes(c.type))) setDirty(true);
      if (changes.some((c) => c.type === 'remove' && c.id === selectedId)) setSelectedId(null);
    },
    [activeView, selectedId, updateView]
  );

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
      const r = await api.runProject(current);
      if (r.mode === 'realtime') {
        // device graph: base host started a realtime session
        setRt({ running: true, logs: [], probes: {} });
        setStatus('实时运行中（含设备组件，调参数即时生效）');
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
  }, [current, ensureSaved]);

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
      setLog({
        title: '编译后运行输出',
        lines: [r.stdout, r.stderr ? `stderr:\n${r.stderr}` : null].filter(Boolean),
      });
    } catch (e) {
      setStatus('编译后运行失败');
      setLog({ title: '编译后运行错误', lines: [api.errorDetail(e)] });
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

  // ---------------------------------------------------------- render

  const selectedNode = view.nodes.find((nd) => nd.id === selectedId) || null;

  return (
    <div className="app">
      <div className="toolbar">
        <h1>Orpheus</h1>
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
        <span className="toolbar-sep" />
        <span title="左键拖拽框选；按住空格+左键或右键/中键拖拽平移；Shift 点击多选">
          <button onClick={wrapSelection} disabled={!current || selectedIds.length === 0}>
            包装为子组件
          </button>
        </span>
        <button onClick={createSub} disabled={!current}>
          新建子组件
        </button>
        <span className="toolbar-sep" />
        <button onClick={doSave} disabled={!dirty || !current || saving}>
          {saving ? '保存中…' : '保存'}
        </button>
        <label className="autosave">
          <input type="checkbox" checked={autoSave} onChange={(e) => setAutoSave(e.target.checked)} />
          自动保存
        </label>
        <button onClick={() => setShowSettings(true)} disabled={!current || !doc} title="工程全局设置（采样率/块长度/缓冲）">
          ⚙ 设置
        </button>
        <button onClick={doCompile} disabled={!current}>
          编译
        </button>
        <button className="primary" onClick={doRun} disabled={!current || rt.running}>
          ▶ 运行
        </button>
        <button onClick={doRunGenerated} disabled={!current || rt.running}>
          ⚙ 编译后运行
        </button>
        {rt.running && (
          <button className="danger-tool" onClick={doRtStop}>
            ■ 停止
          </button>
        )}
        {current && (
          <a className="button" href={api.downloadUrl(current)} download>
            下载 zip
          </a>
        )}
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
        <Palette components={catalog} subsMeta={subsMeta} onDeleteSub={deleteSub} />
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
            onSelectionChange={({ nodes: sn }) => setSelectedIds((sn || []).map((n) => n.id))}
            nodeTypes={nodeTypes}
            deleteKeyCode={['Delete', 'Backspace']}
            selectionOnDrag={!spacePressed}
            panOnDrag={spacePressed ? [1, 2] : [2]}
            multiSelectionKeyCode="Shift"
            fitView
          >
            <Background />
            <Controls />
            <MiniMap />
          </ReactFlow>
        </div>
        <div className="rightbar">
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
            ctx={paramCtx}
          />
        </div>
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
      {(log || outputs.length > 0 || rt.logs.length > 0 || rt.running) && (
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
