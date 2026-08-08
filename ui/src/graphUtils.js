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

/** graph {nodes, connections} -> { nodes, edges } for React Flow. */
export function graphToFlow(graph, catalogById) {
  const nodes = (graph?.nodes || []).map((n) => {
    const comp = catalogById[n.component];
    return {
      id: n.id,
      type: 'orpheus',
      position: { x: n.position?.x ?? 100, y: n.position?.y ?? 100 },
      data: {
        label: n.label || n.id,
        component: n.component,
        params: n.params || {},
        clockSource: !!comp?.clock_source,
        ports: resolvePorts(comp, n.params),
        parameters: comp?.parameters || [],
      },
    };
  });
  const edges = (graph?.connections || []).map((c) => {
    const [source, sourceHandle] = c.from.split(':');
    const [target, targetHandle] = c.to.split(':');
    return { id: `e-${c.from}-${c.to}`, source, target, sourceHandle, targetHandle };
  });
  return { nodes, edges };
}

/** React Flow nodes/edges -> graph {nodes, connections}. */
export function flowToGraph(nodes, edges) {
  return {
    nodes: nodes.map((n) => ({
      id: n.id,
      component: n.data.component,
      task: 'default',
      params: n.data.params || {},
      position: { x: Math.round(n.position.x), y: Math.round(n.position.y) },
      ...(n.data.label && n.data.label !== n.id ? { label: n.data.label } : {}),
    })),
    connections: edges
      .filter((e) => e.sourceHandle && e.targetHandle)
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
  const views = { main: graphToFlow(doc.graph, catalogById) };
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
