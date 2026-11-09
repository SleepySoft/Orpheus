import { isControlHandle, ctlParamId, isDriveEdge, resolveExprValue, isHostDeliveryStatus } from './graphUtils';

export const accessText = (resource) => ({
  read: '只读', write: '只写', read_write: '可读写',
}[resource.access] || '权限未显式声明');

export const canEditParameter = (parameter) => parameter.access !== 'read' && !parameter.readonly;
export const canEditDesignParameter = (parameter, data) => canEditParameter(parameter)
  && !isHostDeliveryStatus(parameter, data);

export const isInitializationParameter = (parameter) => parameter.update_policy === 'restart_required'
  || !!parameter.affects_signature;

/** 参数只因实际连线强制显露；公开映射不决定画布显示，也不授予连接权限。 */
export function interfaceState(node, edges = []) {
  const data = node.data;
  const lockedParameters = new Set();
  const lockedPorts = new Set(Object.keys(data.exportedAudioPorts || {}));
  const connectedControlSources = new Set();
  const connectedControlTargets = new Set();
  const upstreamPorts = new Set();
  for (const edge of edges) {
    if (isDriveEdge(edge)) continue;
    for (const [endpoint, handle, source] of [[edge.source, edge.sourceHandle, true], [edge.target, edge.targetHandle, false]]) {
      if (endpoint !== node.id) continue;
      if (edge.type === 'control' && isControlHandle(handle)) {
        lockedParameters.add(ctlParamId(handle));
        (source ? connectedControlSources : connectedControlTargets).add(ctlParamId(handle));
      }
      else lockedPorts.add(handle);
    }
    if (edge.target === node.id && edge.type !== 'control') upstreamPorts.add(edge.targetHandle);
  }
  return {
    lockedParameters: [...lockedParameters], lockedPorts: [...lockedPorts],
    connectedControlSources: [...connectedControlSources], connectedControlTargets: [...connectedControlTargets],
    upstreamPorts: [...upstreamPorts],
    exposedParameters: [...new Set([...(data.interfaceUi?.exposedParameters || []), ...lockedParameters])],
    exposedPorts: [...new Set([...(data.interfaceUi?.exposedPorts || []), ...lockedPorts])],
    exposedBulkSlots: data.interfaceUi?.exposedBulkSlots || [],
  };
}

const signatureValue = (expression, data) => {
  const value = resolveExprValue(expression, data.params, { parameters: data.parameters });
  const numeric = typeof value === 'number' ? value
    : typeof value === 'string' && /^\d+$/.test(value) ? Number(value) : NaN;
  return Number.isSafeInteger(numeric) && numeric > 0
    ? String(numeric) : `未确定${expression === undefined ? '' : ` (${expression})`}`;
};

export function portSignature(port, data) {
  return `${port.type || 'audio'} · ${port.format || port.sample_format || '格式未声明'} · `
    + `${signatureValue(port.channels, data)}ch · `
    + `${signatureValue(port.block_size ?? data.blockSize, data)}帧`;
}

export function bindingStatus(port, data) {
  const binding = port.host_binding;
  if (!binding) return null;
  const active = Object.entries(binding.when || {}).every(([id, value]) => {
    const actual = data.params?.[id] ?? data.parameters?.find((p) => p.id === id)?.default;
    return String(actual) === String(value);
  });
  if (!active) return '当前配置下宿主绑定不生效';
  const hasUpstream = (data.interfaces?.upstreamPorts || []).some((id) => id === port.id
    || (data.ports || []).some((input) => input.id === id && input.direction === 'input'
      && (!input.host_binding || input.host_binding.slot === binding.slot)));
  if (binding.access === 'write' && hasUpstream)
    return '上游供数，禁止宿主写入';
  const observed = data.probe?.[binding.status];
  const status = observed === undefined ? '未观测交付状态' : `${binding.status}=${String(observed)}`;
  return binding.access === 'write'
    ? `无上游时可由宿主交付；宿主写入不触发 Task · ${status} · 此面板不提交 PCM`
    : `宿主读取 · ${status}`;
}
