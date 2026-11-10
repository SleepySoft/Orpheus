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
  const key = document ? JSON.stringify([project, viewKey, interfaceSnapshot(document)]) : '';
  const [result, setResult] = useState(null);
  useEffect(() => {
    if (!key) return undefined;
    const [name, view, snapshot] = JSON.parse(key);
    if (!name) return undefined;
    const controller = new AbortController();
    let active = true;
    const timer = setTimeout(() => {
      resolveProjectInterfaces(name, snapshot, view, { signal: controller.signal }).then((response) => {
        if (active) setResult({ key, ...response });
      }).catch((error) => {
        if (active) setResult({
          key, error: String(error.response?.data?.detail || error.message || '接口解析请求失败'),
        });
      });
    }, 180);
    return () => { active = false; clearTimeout(timer); controller.abort(); };
  }, [key]);
  return result?.key === key ? result : null;
}

export function resolvedNodeInterfaces(data, result, nodeId) {
  const node = result?.nodes?.[nodeId];
  if (node?.status === 'resolved') return { ports: node.ports, portResolution: node };
  return {
    ports: data.ports,
    portResolution: result?.error ? { status: 'error', message: result.error }
      : node || { status: 'pending', message: '等待后端解析端口引用' },
  };
}
