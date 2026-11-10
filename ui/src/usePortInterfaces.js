import { useEffect, useState } from 'react';
import { resolveProjectInterfaces } from './api';

export function interfaceSnapshot(document) {
  const graphSnapshot = (graph = {}) => ({
    ...graph,
    nodes: (graph.nodes || []).map(({ position, label, todos, ...node }) => node),
  });
  const { layout, metadata, lesson, ...snapshot } = document;
  return {
    ...snapshot,
    graph: graphSnapshot(snapshot.graph),
    subcomponents: (snapshot.subcomponents || []).map(({ layout: subLayout, ...sub }) => ({
      ...sub, graph: graphSnapshot(sub.graph),
    })),
  };
}

export default function usePortInterfaces(project, document, viewKey) {
  const documentKey = document ? JSON.stringify([project, interfaceSnapshot(document)]) : '';
  const [results, setResults] = useState({ documentKey: '', views: {} });
  const [failure, setFailure] = useState(null);
  const cached = results.documentKey === documentKey ? results.views[viewKey] : null;
  useEffect(() => {
    if (!documentKey || cached) return undefined;
    const [name, snapshot] = JSON.parse(documentKey);
    if (!name) return undefined;
    setFailure(null);
    const controller = new AbortController();
    let active = true;
    const timer = setTimeout(() => {
      resolveProjectInterfaces(name, snapshot, viewKey, { signal: controller.signal }).then((response) => {
        if (active) setResults((previous) => ({
          documentKey,
          views: {
            ...(previous.documentKey === documentKey ? previous.views : {}),
            [viewKey]: response,
          },
        }));
      }).catch((error) => {
        if (active) setFailure({
          documentKey, viewKey,
          error: String(error.response?.data?.detail || error.message || '接口解析请求失败'),
        });
      });
    }, 180);
    return () => { active = false; clearTimeout(timer); controller.abort(); };
  }, [documentKey, viewKey, cached]);
  return cached || (failure?.documentKey === documentKey && failure.viewKey === viewKey ? failure : null);
}

export function resolvedNodeInterfaces(data, result, nodeId) {
  const node = result?.nodes?.[nodeId];
  if (node?.status === 'resolved') return { ports: node.ports, portResolution: node };
  return {
    ports: data.ports,
    portResolution: result?.error ? { status: 'error', message: result.error }
      : node || (result
        ? { status: 'error', message: `后端解析结果缺少节点：${nodeId}` }
        : { status: 'pending', message: '后端正在解析当前视图的端口引用，无需编译或运行' }),
  };
}
