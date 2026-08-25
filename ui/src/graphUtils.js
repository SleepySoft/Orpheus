/** Conversion between project documents (with subcomponents) and React Flow views. */

export const SUB_PREFIX = 'sub:';

export const isSubRef = (component) => component?.startsWith(SUB_PREFIX);
export const subIdOf = (component) => component.slice(SUB_PREFIX.length);
export const subViewKey = (subId) => `${SUB_PREFIX}${subId}`;

/** Default parameter values from a component manifest's parameter schema. */
export function defaultParams(component) {
  const params = {};
  for (const p of component.parameters || []) {
    if (p.default !== undefined) params[p.id] = p.default;
  }
  return params;
}

/** Resolve a manifest expression like 'param:channels' against node params. */
export function resolveExprValue(expr, params, component) {
  if (typeof expr === 'string' && expr.startsWith('param:')) {
    const pid = expr.slice(6);
    if (params && params[pid] !== undefined) return params[pid];
    const p = (component?.parameters || []).find((sp) => sp.id === pid);
    return p?.default;
  }
  return expr;
}

/** 控制链路 handle id 前缀：ctl:<paramId>（与音频端口 handle 严格区分）。 */
export const CTL_PREFIX = 'ctl:';
export const isControlHandle = (h) => typeof h === 'string' && h.startsWith(CTL_PREFIX);
export const ctlParamId = (h) => (isControlHandle(h) ? h.slice(CTL_PREFIX.length) : h);

/**
 * 求值形状声明（[int | 'param:xxx', ...]）为具体维度。
 * 返回 int[]（[] 表示标量）；任一元素求值失败（非有限整数）返回 null。
 */
export function resolveShape(shapeList, params, component) {
  if (!Array.isArray(shapeList) || shapeList.length === 0) return [];
  const dims = [];
  for (const el of shapeList) {
    const v = resolveExprValue(el, params, component);
    const n = typeof v === 'number' ? v : parseInt(v, 10);
    if (!Number.isFinite(n)) return null;
    dims.push(n);
  }
  return dims;
}

/** 形状的可读文本：标量 '·'、'[2]'、'[2×2]'；求值失败（null）→ '?'。 */
export function shapeText(shape) {
  if (shape === null || shape === undefined) return '?';
  if (shape.length === 0) return '·';
  return `[${shape.join('×')}]`;
}

/** 两个求值后的形状是否严格相等（null 视为不相等）。 */
export function shapeEquals(a, b) {
  if (a === null || b === null || a === undefined || b === undefined) return false;
  return a.length === b.length && a.every((v, i) => v === b[i]);
}

/**
 * Expand a component's ports for given node params: a port with `count`
 * (e.g. param:channels) is replicated as <id>0..<id>N-1 (variable pins).
 */
export function resolvePorts(component, params) {
  const ports = [];
  for (const p of component?.ports || []) {
    if (p.count !== undefined) {
      const n = Math.max(1, parseInt(resolveExprValue(p.count, params, component), 10) || 1);
      for (let i = 0; i < n; i++) {
        const { count, ...rest } = p;
        ports.push({ ...rest, id: `${p.id}${i}` });
      }
    } else {
      ports.push(p);
    }
  }
  return ports;
}

/** Present a subcomponent definition like a catalog component (ports, no params). */
export function subCatalogEntry(sub) {
  return {
    id: subViewKey(sub.id),
    name: sub.name || sub.id,
    description: sub.description || '',
    version: '',
    ports: (sub.ports || []).map((p) => ({ id: p.id, direction: p.direction, type: 'audio' })),
    parameters: [],
    sub: true,
  };
}

export function mergedCatalog(globalComponents, subsMeta) {
  return [...globalComponents, ...subsMeta.map(subCatalogEntry)];
}

/** graph {nodes, connections} + 顶层 control_connections -> { nodes, edges } for React Flow. */
export function graphToFlow(graph, catalogById, controlConnections = []) {
  const nodes = (graph?.nodes || []).map((n) => {
    const comp = catalogById[n.component];
    return {
      id: n.id,
      type: 'orpheus',
      position: { x: n.position?.x ?? 100, y: n.position?.y ?? 100 },
      data: {
        label: n.label || n.id,
        component: n.component,
        missing: !comp,
        params: n.params || {},
        clockSource: !!comp?.clock_source,
        ports: resolvePorts(comp, n.params),
        parameters: comp?.parameters || [],
        // 替代组声明（同图内节点 id 列表）与组件平台标签，往返保留
        alters: Array.isArray(n.alters) ? n.alters : [],
        platforms: comp?.platforms || [],
      },
    };
  });
  const edges = (graph?.connections || []).map((c) => {
    const [source, sourceHandle] = c.from.split(':');
    const [target, targetHandle] = c.to.split(':');
    return { id: `e-${c.from}-${c.to}`, source, target, sourceHandle, targetHandle };
  });
  // 控制连接 → 控制边（id 前缀 ec- 避免与音频边冲突；handle 带 ctl: 前缀）
  const controlEdges = (controlConnections || []).map((c) => {
    const [source, srcParam] = c.from.split(':');
    const [target, dstParam] = c.to.split(':');
    return {
      id: `ec-${c.from}-${c.to}`,
      source,
      target,
      sourceHandle: `${CTL_PREFIX}${srcParam}`,
      targetHandle: `${CTL_PREFIX}${dstParam}`,
      type: 'control',
    };
  });
  return { nodes, edges: [...edges, ...controlEdges] };
}

/** React Flow nodes/edges -> graph {nodes, connections}（控制边不进 connections，由 viewsToDoc 单独写回顶层段）。 */
export function flowToGraph(nodes, edges) {
  return {
    nodes: nodes.map((n) => ({
      id: n.id,
      component: n.data.component,
      task: 'default',
      params: n.data.params || {},
      position: { x: Math.round(n.position.x), y: Math.round(n.position.y) },
      ...(n.data.label && n.data.label !== n.id ? { label: n.data.label } : {}),
      // alters 非空数组时写回节点，空则省略
      ...(Array.isArray(n.data.alters) && n.data.alters.length ? { alters: n.data.alters } : {}),
    })),
    connections: edges
      .filter((e) => e.sourceHandle && e.targetHandle && e.type !== 'control')
      .map((e) => ({
        from: `${e.source}:${e.sourceHandle}`,
        to: `${e.target}:${e.targetHandle}`,
      })),
  };
}

/**
 * project document -> { views, subsMeta }.
 * views: { main: {nodes, edges}, 'sub:<id>': {nodes, edges}, ... }
 */
export function docToViews(doc, globalComponents) {
  const subsMeta = (doc.subcomponents || []).map((s) => ({
    id: s.id,
    name: s.name || s.id,
    description: s.description || '',
    ports: s.ports || [],
  }));
  const catalogById = Object.fromEntries(
    mergedCatalog(globalComponents, subsMeta).map((c) => [c.id, c])
  );
  const views = { main: graphToFlow(doc.graph, catalogById, doc.control_connections) };
  for (const s of doc.subcomponents || []) {
    views[subViewKey(s.id)] = graphToFlow(s.graph, catalogById);
  }
  return { views, subsMeta };
}

/** views + subsMeta + base document -> full project document. */
export function viewsToDoc(views, subsMeta, baseDoc) {
  const doc = {
    ...baseDoc,
    graph: flowToGraph(views.main?.nodes || [], views.main?.edges || []),
  };
  // 控制连接只存在于主图（子组件视图不参与）：剥掉 ctl: 前缀写回顶层段，空则省略
  const controlEdges = (views.main?.edges || []).filter(
    (e) => e.type === 'control' && isControlHandle(e.sourceHandle) && isControlHandle(e.targetHandle)
  );
  if (controlEdges.length > 0) {
    doc.control_connections = controlEdges.map((e) => ({
      from: `${e.source}:${ctlParamId(e.sourceHandle)}`,
      to: `${e.target}:${ctlParamId(e.targetHandle)}`,
    }));
  } else {
    delete doc.control_connections;
  }
  if (subsMeta.length > 0) {
    doc.subcomponents = subsMeta.map((s) => {
      const view = views[subViewKey(s.id)] || { nodes: [], edges: [] };
      return {
        id: s.id,
        name: s.name,
        description: s.description || '',
        ports: s.ports,
        graph: flowToGraph(view.nodes, view.edges),
      };
    });
  } else {
    delete doc.subcomponents;
  }
  return doc;
}
