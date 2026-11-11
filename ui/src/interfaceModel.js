import { isControlHandle, ctlParamId, isDriveEdge, isDriveHandle, isHostDeliveryStatus } from './graphUtils';

export const accessText = (resource) => ({
  read: '只读', write: '只写', read_write: '可读写',
}[resource.access] || '权限未显式声明');

export const canEditParameter = (parameter) => parameter.access !== 'read' && !parameter.readonly;
export const canEditDesignParameter = (parameter, data) => canEditParameter(parameter)
  && !isHostDeliveryStatus(parameter, data);

export const isInitializationParameter = (parameter) => parameter.update_policy === 'restart_required'
  || !!parameter.affects_signature;

export function componentDataPoints(component = {}) {
  const slots = component.bulk_slots || [];
  const slotIds = new Set(slots.map((slot) => slot.id));
  const initializers = new Map((component.parameters || [])
    .filter((parameter) => slotIds.has(parameter.bulk?.slot_key))
    .map((parameter) => [parameter.bulk.slot_key, parameter]));
  const initializationIds = new Set([...initializers.values()].map((parameter) => parameter.id));
  return [
    ...(component.parameters || []).filter((parameter) => !initializationIds.has(parameter.id)),
    ...slots.map((slot) => ({
      ...slot, runtime: true,
      ...(initializers.has(slot.id) ? { initialization: initializers.get(slot.id) } : {}),
    })),
  ];
}

/** 参数只因实际连线强制显露；公开映射不决定画布显示，也不授予连接权限。 */
export function interfaceState(node, edges = []) {
  const data = node.data;
  const lockedParameters = new Set();
  const lockedPorts = new Set(Object.keys(data.exportedAudioPorts || {}));
  const connectedControlSources = new Set();
  const connectedControlTargets = new Set();
  const upstreamPorts = new Set();
  for (const edge of edges) {
    if (isDriveEdge(edge) && (isDriveHandle(edge.sourceHandle) || isDriveHandle(edge.targetHandle))) continue;
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

function configuredPortValue(port, field, data) {
  let value = port[field] ?? (field === 'sample_format' ? port.format : undefined);
  if (typeof value === 'string' && /^param:[\w]+$/.test(value)) {
    const id = value.slice(6);
    value = data.params?.[id] ?? data.parameters?.find((parameter) => parameter.id === id)?.default;
  }
  if (['channels', 'block_size', 'sample_rate'].includes(field)) {
    return value !== undefined && value !== null && value !== ''
      && ['number', 'string'].includes(typeof value) && Number.isFinite(Number(value))
      && !(field === 'sample_rate' && Number(value) === 0) ? Number(value) : undefined;
  }
  return typeof value === 'string' && !value.includes(':') ? value : undefined;
}

export function portSignature(port, data) {
  if (port.type === 'trigger') {
    const error = data.portResolution?.status === 'error' ? ' · 解析失败'
      : port.resolution?.status === 'conflict' ? ' · 约束冲突' : '';
    return `触发（无 PCM）${error}`;
  }
  if (data.portResolution?.status === 'error') return `解析失败：${data.portResolution.message}`;
  if (data.portResolution?.status === 'pending'
    || !['resolved', 'conflict'].includes(port.resolution?.status)) {
    const type = configuredPortValue(port, 'type', data);
    const format = configuredPortValue(port, 'sample_format', data);
    const channels = configuredPortValue(port, 'channels', data);
    const frames = configuredPortValue(port, 'block_size', data);
    const rate = configuredPortValue(port, 'sample_rate', data);
    return `配置：${type || '类型解析中'} · ${format || '格式解析中'} · `
      + `${channels === undefined ? '通道解析中' : `${channels}ch`} · `
      + `${frames === undefined ? '块长解析中' : `${frames}帧`} · `
      + `${rate === undefined ? '采样率解析中' : `${rate}Hz`} · 连线校验中`;
  }
  const actual = `${port.type} · ${port.sample_format} · ${port.channels}ch · `
    + `${port.frames}帧 · ${port.sample_rate}Hz`;
  return port.resolution.status === 'conflict' ? `${actual} · 约束冲突` : actual;
}

export function validateTriggerConnection(nodes, connection) {
  const source = nodes.find((node) => node.id === connection.source)?.data.ports
    ?.find((port) => port.id === connection.sourceHandle);
  const target = nodes.find((node) => node.id === connection.target)?.data.ports
    ?.find((port) => port.id === connection.targetHandle);
  if ((source?.type === 'trigger' || target?.type === 'trigger')
    && (source?.type !== 'trigger' || target?.type !== 'trigger')) {
    return '纯触发接口不传输 PCM，当前只支持触发 → 触发连接；不能直接连到音频或其它类型接口，也不是旧的虚拟执行引脚';
  }
  return null;
}

export function portOrigin(port) {
  return ['resolved', 'conflict'].includes(port.resolution?.status)
    ? `引用来源：${port.resolution.endpoint}（${port.resolution.component}）`
      + (port.resolution.message ? `；${port.resolution.message}` : '') : '';
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
