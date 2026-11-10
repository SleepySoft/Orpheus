import React, { useState } from 'react';
import * as api from './api';

export const INITIAL_CONFIGURATION = `id: project.custom.effect
name: 自建音效
version: 0.1.0
category: 工程/自建组件
interfaces:
  input:
    type: audio
    direction: input
    channels: 1
    process: true
  output:
    type: audio
    direction: output
    channels: input:channels
  gain:
    type: float
    default: 1.0
    update_policy: immediate
`;

export const TRIGGER_CONFIGURATION = `id: project.custom.trigger
name: 纯触发组件
version: 0.1.0
category: 工程/自建组件
interfaces:
  tick:
    type: trigger
    direction: input
    process: true
  next:
    type: trigger
    direction: output
    drive_port_no: 1
`;

export default function SourceComponentDialog({ project, readOnly, onRegistered, onClose }) {
  const [pkg, setPkg] = useState('my_effect');
  const [configuration, setConfiguration] = useState(INITIAL_CONFIGURATION);
  const [revision, setRevision] = useState(null);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  const [registered, setRegistered] = useState(false);
  const validPackage = /^[a-z][a-z0-9_]{0,63}$/.test(pkg)
    && !/^(con|prn|aux|nul|com[1-9]|lpt[1-9])$/.test(pkg);

  const perform = async (operation) => {
    if (busy || readOnly || !validPackage) return;
    setBusy(true);
    setRegistered(false);
    const token = api.getEditToken();
    let saved = false;
    try {
      if (operation === 'load') {
        const loaded = await api.readSourceComponent(project, pkg);
        setConfiguration(loaded.configuration);
        setRevision(loaded.sha256);
        setMessage('已载入落盘配置，可修订后重新保存和生成。');
        return;
      }
      const result = await api.saveSourceComponent(project, pkg, configuration, revision, token);
      setRevision(result.sha256);
      saved = true;
      setMessage('配置已保存，尚未注册组件。');
      if (operation === 'save') return;
      const report = await api.runSourceComponent(project, pkg, operation, result.sha256, token);
      if (!report.ok) {
        const diagnostics = (report.diagnostics || [])
          .map((item) => `${item.location}: ${item.message}`).join('\n');
        const phase = { validation: '校验', generation: '生成', registration: '注册' }[report.phase] || '操作';
        setMessage(`配置已保存；${phase}失败，未注册新组件。可修订后重试。\n${report.detail || diagnostics}`);
        return;
      }
      if (!report.registered) {
        setMessage('配置已保存且校验通过，尚未生成或注册组件。');
        return;
      }
      if (token !== api.getEditToken()) throw new Error('编辑权已变化，请接管并刷新');
      await onRegistered(report);
      setRegistered(true);
      setMessage(`生成并注册成功：${report.component.name}。可从组件库拖入画布。`);
    } catch (error) {
      setMessage(`${saved ? '配置已保存；后续操作失败，未确认注册成功' : '配置保存或载入失败'}：${api.errorDetail(error)}`);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="modal-backdrop" onKeyDown={(event) => event.stopPropagation()}>
      <div className="modal source-component-dialog" role="dialog" aria-modal="true" aria-label="新建源码组件">
        <h3>新建源码组件</h3>
        <p>配置是唯一输入；先保存，再调用独立脚本校验、生成。算法文件只在首次生成时创建，不会覆盖。</p>
        <p>支持 audio/control/bulk/debug 数据接口（f32/i16/i32）、trigger 纯触发接口，
          以及 float/int/bool 标量和定长 string 参数；完整约束以独立校验/生成脚本诊断为准。
          trigger 不携带 PCM，不配置通道数、块长或样本格式；驱动出口编号仍用 drive_port_no。
          当前只支持触发接口间连接，不使用旧虚拟 drv:out/in 引脚。</p>
        <button disabled={busy || readOnly} onClick={() => {
          setConfiguration(TRIGGER_CONFIGURATION);
          setRegistered(false);
          setMessage('已填入纯触发配置示例，请确认组件 ID 后保存；算法中仍须实现实际触发逻辑。');
        }}>填入纯触发示例</button>
        <label>包名
          <input aria-label="包名" value={pkg} disabled={busy || readOnly || revision !== null}
            onChange={(event) => setPkg(event.target.value)} />
        </label>
        <p>保存位置：{project}/components/{pkg}/component.yaml</p>
        {!validPackage && <p role="alert">包名仅含小写字母、数字、下划线，以字母开头，不可使用系统保留名。</p>}
        <label>组件配置（YAML）
          <textarea aria-label="组件配置" spellCheck={false} value={configuration}
            disabled={busy || readOnly}
            onChange={(event) => { setConfiguration(event.target.value); setRegistered(false); }} />
        </label>
        <pre role="status">{message}</pre>
        {readOnly && <p role="alert">当前没有编辑权，请关闭并接管刷新后重试。</p>}
        <div className="modal-actions">
          <button disabled={busy || readOnly || !validPackage} onClick={() => perform('load')}>载入已有配置</button>
          <button disabled={busy || readOnly || !validPackage} onClick={() => perform('save')}>保存配置</button>
          <button disabled={busy || readOnly || !validPackage} onClick={() => perform('validate')}>保存并校验</button>
          <button disabled={busy || readOnly || !validPackage || registered} onClick={() => perform('generate')}>
            {busy ? '处理中…' : '保存并生成'}
          </button>
          <button disabled={busy} onClick={onClose}>关闭</button>
        </div>
      </div>
    </div>
  );
}
