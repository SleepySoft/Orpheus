import React from 'react';
import { createPortal } from 'react-dom';
import { Handle, Position, NodeResizer, useUpdateNodeInternals } from 'reactflow';
import { resolveShape, shapeText, CTL_PREFIX, driveHandles, DRIVE_IN, DRIVE_OUT } from './graphUtils';
import { NODE_WIDGETS } from './nodeWidgets';
import { NodeActionsContext } from './NodeActionsContext';
import { PLACEHOLDER_ID } from './placeholder';
import { interfaceState, portSignature, portOrigin, bindingStatus, accessText, canEditDesignParameter, isInitializationParameter } from './interfaceModel';

/** Custom React Flow node: ports come from the component manifest (resolved). */
export default function OrpheusNode({ id, data, selected }) {
  const [enlarged, setEnlarged] = React.useState(false);
  const interfaces = data.interfaces || interfaceState({ id, data });
  const ports = (data.ports || []).filter((port) => interfaces.exposedPorts.includes(port.id));
  const visibleParameters = (data.parameters || []).filter((p) => interfaces.exposedParameters.includes(p.id));
  const initializationParameters = visibleParameters.filter(isInitializationParameter);
  const runtimeParameters = visibleParameters.filter((p) => !isInitializationParameter(p));
  const visibleBulkSlots = (data.bulkSlots || []).filter((slot) => interfaces.exposedBulkSlots.includes(slot.id));
  const inputs = ports.filter((p) => p.direction === 'input');
  const outputs = ports.filter((p) => p.direction === 'output');
  const isSub = (data.component || '').startsWith('sub:');
  const isBridge = !!data.bridgeConfig;
  const isPlaceholder = data.packageType === 'placeholder' || data.component === PLACEHOLDER_ID;
  const blockerText = (data.blockers || [])
    .map((item) => `${item.gate || item.id}: ${item.reason}`)
    .join('\n');
  const platformTone = (() => {
    const platforms = data.platforms || [];
    if (platforms.length !== 1) return platforms.length > 1 ? 'multi' : '';
    if (platforms[0] === 'win') return 'win';
    if (platforms[0] === 'dsp') return 'dsp';
    return 'other';
  })();
  const shortName = data.missing
    ? '未映射组件'
    : isBridge
      ? 'Bridge 配置'
      : isSub
      ? '📦 子组件（双击打开）'
      : (data.component || '').split('.').pop();

  // Each handle is anchored to its own port row (position: relative in CSS),
  // so pins never overlap and each wire lands on a labeled, distinct pin.
  const handleStyle = { top: '50%', transform: 'translateY(-50%)' };
  const renderParameterRow = (p) => {
    const exported = data.exportedControlParameters?.[p.id];
    const initialization = isInitializationParameter(p);
    const targetAllowed = !initialization && p.bindable && canEditDesignParameter(p, data);
    const sourceAllowed = !initialization && p.control_source && p.access !== 'write';
    const exportedClass = !exported ? '' : initialization ? 'export-config-pin'
      : `export-control-pin ${exported.direction === 'input' ? 'export-control-input' : 'export-control-output'}`;
    return (
      <div key={p.id} data-parameter={p.id}
        className={`port-row ${initialization ? 'initial-config-row' : 'ctl-row'} ${p.control_source && !p.bindable ? 'src' : ''} ${exportedClass}`}
        onClick={exported ? (event) => revealExport(event, 'control', exported.id) : undefined}
        title={exported ? `公开${initialization ? '初始化配置' : '运行期参数'} ${exported.id}：点击定位接口定义` : undefined}>
        {(targetAllowed || interfaces.connectedControlTargets?.includes(p.id)) && (
          <Handle type="target" position={Position.Left} id={`${CTL_PREFIX}${p.id}`}
            isConnectable={!!targetAllowed} className={exported && !initialization ? 'export-control-handle' : ''}
            style={{ ...handleStyle, left: exported && !initialization ? -50 : -11 }} />
        )}
        <span className="ctl-name">{p.name || p.id}</span>
        <span className="shape-badge">
          {shapeText(resolveShape(p.shape || [], data.params, { parameters: data.parameters }))}
        </span>
        {initialization && <span className="initial-config-badge">初始化</span>}
        {(sourceAllowed || interfaces.connectedControlSources?.includes(p.id)) && (
          <Handle type="source" position={Position.Right} id={`${CTL_PREFIX}${p.id}`}
            isConnectable={!!sourceAllowed} className={exported && !initialization ? 'export-control-handle' : ''}
            style={{ ...handleStyle, right: exported && !initialization ? -50 : -11 }} />
        )}
      </div>
    );
  };

  // resolved channel count badge, e.g. "out0 · 1ch"
  const channelBadge = (p) => <span className="ch-badge" title={data.portResolution?.message || portOrigin(p)}>
    {data.portResolution?.status === 'error' ? '解析失败' : portSignature(p, data)}
  </span>;


/**
 * Noise-detector node status: inspect last readback probes and map to a
 * visual alert state. Pure observation - this component never processes
 * audio itself, so "coloring" is the only UI feedback the user asked for.
 */
function noiseStatusClass(data) {
  const probe = data.probe;
  if (!probe) return '';
  const comp = data.component || '';
  let level = 0;
  const clicks = probe.clicks ?? 0;
  if (clicks > 0) level = Math.max(level, 2);
  const inc = (lv) => { level = Math.max(level, lv); };

  if (comp === 'orpheus.builtin.noise_detector_ab') {
    const thd = probe.thd_n_db;
    const ratio = probe.noise_ratio ?? 0;
    if (ratio > 0.15) inc(2); else if (ratio > 0.03) inc(1);
    if (typeof thd === 'number') {
      if (thd > -30) inc(2); else if (thd > -50) inc(1);
    }
  } else if (comp === 'orpheus.builtin.noise_detector') {
    const flat = probe.flatness ?? 0;
    if (flat > 0.55) inc(2); else if (flat > 0.3) inc(1);
  } else if (comp === 'orpheus.builtin.noise_detector_nlms') {
    const res = probe.residue_db;
    const ratio = probe.noise_ratio ?? 0;
    if (ratio > 0.15) inc(2); else if (ratio > 0.03) inc(1);
    if (typeof res === 'number') {
      if (res > -15) inc(2); else if (res > -35) inc(1);
    }
  } else {
    return '';
  }
  return level >= 2 ? 'node-alert-danger' : level === 1 ? 'node-alert-warn' : '';
}

const BodyWidget = NODE_WIDGETS[data.component];
  const { showReadme, showControlLinks, revealSubExport, driveLinking } = React.useContext(NodeActionsContext);
  const updateNodeInternals = useUpdateNodeInternals();
  const executionHandles = driveHandles(data, data.flowMode);
  const handleSignature = [
    ...executionHandles,
    ...ports.map((port) => `audio:${port.direction}:${port.id}:${data.exportedAudioPorts?.[port.id]?.id || ''}`),
    ...visibleParameters
      .map((parameter) => `control:${parameter.control_source ? 'source' : ''}:${parameter.bindable ? 'target' : ''}:${parameter.id}:${data.exportedControlParameters?.[parameter.id]?.id || ''}`),
    ...(interfaces.connectedControlSources || []).map((id) => `connected-source:${id}`),
    ...(interfaces.connectedControlTargets || []).map((id) => `connected-target:${id}`),
  ].join('|');

  React.useEffect(() => {
    updateNodeInternals(id);
  }, [handleSignature, id, showControlLinks, driveLinking, updateNodeInternals]);

  // compiled rate badge, e.g. "48kHz" or "24kHz ÷2" (visible time tree)
  // 时钟源（信号/扫频/设备/wav 输入）显示 ⏱ 徽标：图采样率以它为准
  const clockSource = !!data.clockSource;
  const driveDots = (
    <span className="drive-domain-dots" aria-label="声明的驱动归属">
      {(data.driveMemberships || []).map((domain) => (
        <span key={domain.id} className="drive-domain-dot" title={domain.label}
          style={{ backgroundColor: domain.color }} />
      ))}
    </span>
  );
  const driveRouting = executionHandles.map((handle) => (
    <Handle key={handle} type={handle === DRIVE_IN ? 'target' : 'source'}
      position={handle === DRIVE_IN ? Position.Top : Position.Right} id={handle}
      className={`drive-body-anchor ${driveLinking ? 'drive-linking' : ''}`}
      isConnectable={!!driveLinking && (handle === DRIVE_IN || clockSource)}
      isConnectableStart={!!driveLinking && handle === DRIVE_OUT && clockSource}
      isConnectableEnd={!!driveLinking && handle === DRIVE_IN}
      style={{ opacity: 0, pointerEvents: driveLinking && (handle === DRIVE_IN || clockSource) ? 'auto' : 'none' }} />
  ));
  const rateBadge = (() => {
    const r = data.rate;
    if (!r || !r.sample_rate) {
      return clockSource ? (
        <span className="clock-badge" title="时钟源（图采样率以它为准）">⏱ 时钟源</span>
      ) : null;
    }
    const khz = r.sample_rate % 1000 === 0 ? `${r.sample_rate / 1000}k` : `${(r.sample_rate / 1000).toFixed(1)}k`;
    const txt = `${khz}Hz${r.divisor > 1 ? ` ÷${r.divisor}` : ''}`;
    if (clockSource) {
      return (
        <span className="clock-badge" title={`时钟源：图采样率 ${r.sample_rate} Hz（以它为准），分频比 ${r.divisor}`}>
          ⏱ {txt}
        </span>
      );
    }
    return (
      <span className="rate-badge" title={`采样率 ${r.sample_rate} Hz，块量子 ${r.frames} 帧，分频比 ${r.divisor}`}>
        {txt}
      </span>
    );
  })();

  const revealExport = (event, kind, id) => {
    if (!data.exportSubId) return;
    event.stopPropagation();
    revealSubExport(data.exportSubId, kind, id);
  };

  const renderRow = (p, isInput) => {
    const exported = data.exportedAudioPorts?.[p.id];
    return (
      <div
        key={p.id}
        className={`port-row audio-port-row ${exported ? `export-pin ${exported.direction === 'input' ? 'export-input' : 'export-output'}` : ''}`}
        onClick={exported ? (event) => revealExport(event, 'audio', exported.id) : undefined}
        title={exported ? `导出${exported.direction === 'input' ? '输入' : '输出'} ${exported.id}：点击定位接口定义` : undefined}
      >
      {isInput && (
        <Handle
          type="target"
          position={Position.Left}
          id={p.id}
          className={exported ? 'export-handle' : ''}
          style={{ ...handleStyle, left: exported ? -50 : -11 }}
        />
      )}
      <span>{p.id}</span>
      {channelBadge(p)}
      {p.host_binding && <small>{bindingStatus(p, data)}</small>}
      {!isInput && (
        <Handle
          type="source"
          position={Position.Right}
          id={p.id}
          className={exported ? 'export-handle' : ''}
          style={{ ...handleStyle, right: exported ? -50 : -11 }}
        />
      )}
      </div>
    );
  };

  if (data.component === 'orpheus.builtin.task_source') {
    const connectionHint = data.flowMode !== 'task_sequence'
      ? '请在工程设置中启用显式驱动'
      : driveLinking ? '拖向目标组件本体，松手连接' : '先勾选工具栏「驱动连线」，再拖动本体';
    return (
      <div className={`drive-source-marker ${selected ? 'selected' : ''}`}
        aria-label={`纯驱动源：${data.label}`}
        title={`纯触发标记，不传输PCM；${connectionHint}`}>
        <span className="drive-source-symbol">↗</span>
        <div>
          <strong>{data.label}{driveDots}</strong>
          <div className="drive-source-caption">纯触发 · {data.params?.frequency_hz ?? 100} Hz</div>
          <div className="drive-source-caption">{connectionHint}</div>
        </div>
        {driveRouting}
      </div>
    );
  }

  const audioSource = clockSource && (data.ports || []).some((port) => port.direction === 'output' && port.type === 'audio');
  const hostBuffer = data.component === 'orpheus.builtin.host_audio';
  const hostOutput = hostBuffer && Number(data.params?.mode ?? 0) === 1;
  const bufferPort = (data.ports || []).find((port) => port.direction === (hostOutput ? 'input' : 'output') && port.type === 'audio');
  const sourceFrames = bufferPort?.resolution ? bufferPort.frames : null;
  const sourceChannels = bufferPort?.resolution ? bufferPort.channels : null;

  return (
    <>
      {selected && (
        <NodeResizer
          isVisible={selected}
          minWidth={160}
          minHeight={80}
          color="#4cc9f0"
        />
      )}
      <div className={`orpheus-node audio-node ${audioSource ? 'audio-drive-source' : ''} ${showControlLinks ? 'control-mode' : ''} ${selected ? 'selected' : ''} ${isSub ? 'sub' : ''} ${isBridge ? 'bridge-config-node' : ''} ${data.missing ? 'missing' : ''} ${platformTone ? `platform-${platformTone}` : ''} ${noiseStatusClass(data)}`}>
      {driveRouting}
      <div className="node-header">
        <div className="node-title">
          {data.label}
          {driveDots}
          {data.missing && (
            <span className="missing-badge" title="该组件已被删除：请删除此节点或重新创建组件">
              组件缺失
            </span>
          )}
          {isPlaceholder && (
            <span className="placeholder-badge" title={blockerText || '该算法尚未完成蒸馏'}>
              待蒸馏
            </span>
          )}
          {(data.todos || []).some((todo) => todo.status !== 'done') && (
            <span className="placeholder-badge" title="在参数面板查看待办，工程 TODO 可查找全部待办">TODO</span>
          )}
          {rateBadge}
          {(data.portResolution?.status === 'error' || data.portResolution?.diagnostics?.length > 0) && (
            <span className="missing-badge" title={data.portResolution.message
              || data.portResolution.diagnostics.map((item) => item.message).join('\n')}>接口异常</span>
          )}
          {data.hasNote && (
            <span className="note-badge" title="该节点有笔记">
              📝
            </span>
          )}
          {Array.isArray(data.alters) && data.alters.length > 0 && (
            <span
              className="alter-badge"
              title={`替代组：与 ${data.alters.join('、')} 占同一逻辑槽位，按目标平台（工程设置 target）激活其一，未激活成员不参与编译`}
            >
              ⚯ 替代组
            </span>
          )}
          {Array.isArray(data.platforms) && data.platforms.length > 0 && (
            <span className={`platform-badge platform-badge-${platformTone}`} title={`适用平台：${data.platforms.join(' / ')}`}>
              {data.platforms.map((platform) => platform.toUpperCase()).join('/')}
            </span>
          )}
        </div>
        <div className="node-subtitle">{shortName}</div>
        {noiseStatusClass(data) && (
          <span className="node-alert-dot" title="??????" />
        )}
        {data.missing && data.params && data.params.note ? (
          <div className="node-note" title={data.params.note}>
            {data.params.note}
          </div>
        ) : null}
        <div className="node-actions">
          {!data.missing && !isSub && (
            <button
              className="node-info-btn"
              title="查看组件说明"
              onClick={(e) => {
                e.stopPropagation();
                showReadme(data.component);
              }}
            >
              ℹ
            </button>
          )}
          {BodyWidget && (
            <button
              className="monitor-enlarge"
              title="放大监控界面"
              onClick={(e) => {
                e.stopPropagation();
                setEnlarged(true);
              }}
            >
              ⤢
            </button>
          )}
        </div>
      </div>
      {(audioSource || (hostBuffer && ports.length > 0)) && (
        <div className="source-buffer-summary" title={hostBuffer
          ? '纯宿主音频缓冲，不产生驱动；宿主在实际Task调用边界交付或读取完整交错f32块'
          : 'PCM输出绑定与驱动入口相互独立；输入供数由具体源决定'}>
          <span>{hostBuffer ? hostOutput ? '输出 Buffer' : '输入 Buffer' : '音频 Buffer'}</span>
          <strong>{typeof sourceChannels === 'number' ? sourceChannels : '待解析'}ch × {typeof sourceFrames === 'number' ? sourceFrames : '待解析'}帧</strong>
          {hostBuffer && (data.ports || []).filter((port) => port.host_binding).map((port) => (
            <small key={port.id}>{bindingStatus(port, data)}</small>
          ))}
          {hostBuffer && <small>交错 {bufferPort?.resolution ? bufferPort.sample_format : '待解析'} · 独立驱动 · 生成代码含适配 TODO</small>}
          {data.component === 'orpheus.builtin.buffer_source' && <small>宿主交付与驱动独立；此界面不提交 PCM</small>}
        </div>
      )}
      <div className="node-body">
        <div className="node-ports inputs">{inputs.map((p) => renderRow(p, true))}</div>
        <div className="node-ports outputs">{outputs.map((p) => renderRow(p, false))}</div>
      </div>
      {/* 参数显露不授予控制权限；已有旧线保留不可连接锚点。 */}
      {initializationParameters.length > 0 && <div className="node-initial-config">
        <div className="initial-config-title">初始化配置</div>
        {initializationParameters.map(renderParameterRow)}
      </div>}
      {runtimeParameters.length > 0 && <div className="node-controls">
        <div className="control-dock-title">运行期接口</div>
        {runtimeParameters.map(renderParameterRow)}
      </div>}
      {visibleBulkSlots.length > 0 && <div className="node-bulk-interfaces">
        {visibleBulkSlots.map((slot) => (
          <div className="port-row bulk-interface-row" key={slot.id}>
            Buffer：{slot.name || slot.id} · {accessText(slot)}
          </div>
        ))}
      </div>}
      {BodyWidget && <BodyWidget data={data} />}
        {enlarged &&
          createPortal(
            <div className="monitor-overlay" onClick={() => setEnlarged(false)}>
              <div className="monitor-panel" onClick={(e) => e.stopPropagation()}>
                <div className="monitor-title">
                  <span>
                    {data.label} <span className="muted">({shortName})</span>
                  </span>
                  <button className="danger" onClick={() => setEnlarged(false)}>
                    × 关闭
                  </button>
                </div>
                <BodyWidget data={data} large />
              </div>
            </div>,
            document.body
          )}
      </div>
    </>
  );
}
