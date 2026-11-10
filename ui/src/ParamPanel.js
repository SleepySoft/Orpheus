import React from 'react';
import ParamField from './widgets';
import { NodeTodos, isRunnablePlaceholder } from './TodoPanel';
import { accessText, canEditParameter, canEditDesignParameter, isInitializationParameter, portSignature, portOrigin, bindingStatus } from './interfaceModel';
import { isHostDeliveryStatus } from './graphUtils';

// Universal params (channel count, sample rate, ...) always go on top,
// separated from component-specific params.
const UNIVERSAL_IDS = new Set(['channels', 'sample_rate']);
const isUniversalParam = (p) => p.affects_signature || UNIVERSAL_IDS.has(p.id);

/** Right-hand panel: edit the selected node's parameters per its manifest schema. */
export default function ParamPanel({
  node,
  viewKey,
  onParamChange,
  onTodosChange,
  todosReadOnly,
  onNodeNoteChange,
  nodeNotes,
  onDeleteNode,
  onRenameNode,
  tasks,
  onTaskChange,
  flowMode,
  ctx,
  onInterfaceChange,
}) {
  if (!node) {
    return (
      <div className="sidebar">
        <h3>参数面板</h3>
        <p className="muted">选择一个节点编辑参数；从左侧拖入组件添加节点。</p>
      </div>
    );
  }

  const { component, params, parameters } = node.data;
  const noteKey = viewKey === 'main' ? node.id : `${viewKey}/${node.id}`;
  const nodeNote = nodeNotes?.[noteKey] || '';

  const schemaIds = new Set((parameters || []).map((p) => p.id));
  const extraKeys = Object.keys(params || {}).filter((k) => !schemaIds.has(k));
  const ordered = [...(parameters || []).filter(isUniversalParam), ...(parameters || []).filter((p) => !isUniversalParam(p))];
  const initialization = ordered.filter(isInitializationParameter);
  const runtime = ordered.filter((p) => !isInitializationParameter(p) && canEditDesignParameter(p, node.data));
  const observations = ordered.filter((p) => !isInitializationParameter(p) && !canEditDesignParameter(p, node.data));
  const interfaces = node.data.interfaces || {};
  const exposure = (field, id, locked = false) => (
    <label className="interface-exposure">
      <input type="checkbox" aria-label={`显露 ${field}:${id}`}
        checked={(interfaces[field] || []).includes(id)} disabled={locked}
        onChange={(event) => onInterfaceChange?.(field, id, event.target.checked)} />
      图上显露{locked ? field === 'exposedParameters' ? '（已连接）' : '（已连接/已导出）' : ''}
    </label>
  );

  const referenceField = (name, value) => (
    <label className="node-ref-field">
      <span className="muted">{name}</span>
      <input className="node-ref-value" type="text" aria-label={name}
        value={value} title={value} readOnly onFocus={(event) => event.currentTarget.select()} />
    </label>
  );

  const renderField = (schema) => (
    <div key={schema.id} className="declared-interface" data-interface={`parameter:${schema.id}`}>
      <ParamField schema={schema}
        value={isHostDeliveryStatus(schema, node.data) ? node.data.probe?.[schema.id]
          : canEditParameter(schema) ? params?.[schema.id] ?? schema.default
          : node.data.probe?.[schema.id] ?? params?.[schema.id] ?? schema.default}
        disabled={!canEditDesignParameter(schema, node.data)}
        disabledLabel={isHostDeliveryStatus(schema, node.data) ? '宿主交付' : '只读'}
        onChange={onParamChange} ctx={ctx} />
      <small>{schema.id} · {schema.type} · {accessText(schema)}
        {' · '}更新：{schema.update_policy || '未声明'}
        {' · '}shape：{JSON.stringify(schema.shape || [])}
        {schema.bindable ? ' · 可绑定目标' : ''}
        {schema.control_source ? ' · 控制源' : ''}
        {schema.readback ? ' · 支持回读' : ''}
      </small>
      {isInitializationParameter(schema) && <small>
        初始化配置：启动前注入；运行后修改需重新准备，不作为运行期控制端口。
      </small>}
      {node.data.exportedControlParameters?.[schema.id] && <small>
        外层公开：{node.data.exportedControlParameters[schema.id].id}；隐藏图上显示不删除公开映射。
      </small>}
      {isHostDeliveryStatus(schema, node.data) && <small>
        宿主交付状态：{node.data.probe?.[schema.id] === undefined ? '未观测' : String(node.data.probe[schema.id])}；
        设计初始化为 false，由完整 PCM 提交原子更新，此面板不直接写入。
      </small>}
      {exposure('exposedParameters', schema.id, interfaces.lockedParameters?.includes(schema.id))}
    </div>
  );

  return (
    <div className="sidebar">
      <h3>参数面板</h3>
      <div className="node-ref">
        {referenceField('节点 ID', node.id)}
        <div className="node-ref-name">
          {referenceField('显示名', node.data.label || node.id)}
          <button className="rename-btn" onClick={() => onRenameNode(node.id)} title="重命名节点（显示名）">
            重命名
          </button>
        </div>
        {referenceField('组件类型', component)}
      </div>
      {!node.data.bridgeConfig && flowMode !== 'task_sequence' && (
        <div className="param-field">
          <label>所属 Task</label>
          <select value={node.data.task || 'default'} onChange={(e) => onTaskChange(node.id, e.target.value)}>
            {(tasks || []).map((task) => <option key={task.id} value={task.id}>{task.name || task.id}</option>)}
          </select>
        </div>
      )}
      {flowMode === 'task_sequence' && !node.data.bridgeConfig && (
        <p className="muted drive-panel-hint">
          {node.data.clockSource
            ? '驱动源：宿主推进入口；紫色分支可单独分频，不传输PCM。'
            : '驱动箭头指向节点顶部；数据从独立绑定的输入端口读取。'}
          {' '}点击紫色箭头标签可设置周期和相位。
        </p>
      )}
      {initialization.length > 0 && (
        <InterfaceSection title="初始化配置" count={initialization.length}>
          {initialization.map(renderField)}
        </InterfaceSection>
      )}
      {runtime.length > 0 && <InterfaceSection title="运行期参数" count={runtime.length}>
        {runtime.map(renderField)}
      </InterfaceSection>}
      {observations.length > 0 && <InterfaceSection title="观测 / 交付状态" count={observations.length}>
        {observations.map(renderField)}
      </InterfaceSection>}
      <InterfaceSection title="声明的 Buffer 资源" count={(node.data.bulkSlots || []).length}>
      {(node.data.bulkSlots || []).map((slot) => (
        <div key={slot.id} className="declared-interface" data-interface={`bulk:${slot.id}`}>
          <strong>{slot.name || slot.id}</strong>
          <small>{slot.id} · {slot.type || slot.element_type || '类型未声明'} · {accessText(slot)}
            {' · '}shape：{JSON.stringify(slot.shape || [])}
            {' · '}容量：{String(slot.count ?? slot.capacity ?? '未声明')}</small>
          {exposure('exposedBulkSlots', slot.id)}
        </div>
      ))}
      </InterfaceSection>
      <InterfaceSection title="数据端口签名（逐端口）" count={(node.data.ports || []).length}>
      {node.data.portResolution?.status === 'error'
        && <p role="status">数据端口解析失败：{node.data.portResolution.message}</p>}
      {(node.data.portResolution?.diagnostics || []).map((item) => (
        <p role="status" key={item.origin}>接口约束冲突：{item.message}</p>
      ))}
      {(node.data.ports || []).map((port) => (
        <div key={port.id} className="declared-interface" data-interface={`port:${port.id}`}>
          <strong>{port.id} · {port.direction === 'input' ? '输入' : '输出'}</strong>
          <small>{portSignature(port, node.data)}</small>
          {portOrigin(port) && <small>{portOrigin(port)}</small>}
          {port.host_binding && <small>
            {(node.data.bulkSlots || []).some((slot) => slot.id === port.host_binding.slot)
              ? '绑定已声明资源' : '绑定资源未在目录声明'}：{port.host_binding.slot} · {port.host_binding.access} · {port.host_binding.status}
            <br />{bindingStatus(port, node.data)}
          </small>}
          {exposure('exposedPorts', port.id, interfaces.lockedPorts?.includes(port.id))}
        </div>
      ))}
      </InterfaceSection>
      <p className="muted">仅展示 manifest 声明的接口，不枚举 C 私有状态。显露不改变访问或控制权限。</p>
      <NodeTodos todos={node.data.todos} onChange={onTodosChange} isPlaceholder={isRunnablePlaceholder(node)} readOnly={todosReadOnly} />
      <NodeNoteSection
        viewKey={viewKey}
        nodeId={node.id}
        note={nodeNote}
        onChange={onNodeNoteChange}
      />
      {extraKeys.map((key) => (
        <ParamField
          key={key}
          schema={{ id: key, name: key, type: 'string' }}
          value={params[key]}
          onChange={onParamChange}
          ctx={ctx}
        />
      ))}
      <button className="danger" onClick={() => onDeleteNode(node.id)}>
        删除节点
      </button>
    </div>
  );
}

function InterfaceSection({ title, count, children }) {
  return (
    <details className="interface-section" open>
      <summary>{title}（{count}）</summary>
      <div className="interface-section-grid">{children}</div>
    </details>
  );
}

/** Editable note for a single node instance; persisted in node-notes.json, not project.yaml. */
function NodeNoteSection({ viewKey, nodeId, note, onChange }) {
  return (
    <details className="node-notes-section" open={!!note}>
      <summary>节点笔记 {note ? '(已填写)' : ''}</summary>
      <textarea
        className="node-notes-textarea"
        placeholder={`记录 ${nodeId} 在此工程中的作用、参数讲究等…`}
        value={note}
        onChange={(e) => onChange(viewKey, nodeId, e.target.value)}
        rows={4}
      />
      <p className="muted" style={{ fontSize: 11, margin: '4px 0 0' }}>
        保存在 <code>node-notes.json</code>，不写入 <code>project.yaml</code>。
      </p>
    </details>
  );
}
