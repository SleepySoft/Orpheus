import React, { useState } from 'react';
import { collectViewTodos, newTodo, PLACEHOLDER_ID, placeholderPorts, DEFAULT_INTERFACE } from './placeholder';

const STATUS = { pending: '待处理', in_progress: '进行中', blocked: '阻塞', done: '已完成' };
const FIELDS = {
  title: '任务', source_ref: '源码索引（相对路径、符号或索引 ID）', source_hash: '源版本 / SHA-256',
  intent: '意图 / 预期行为', next_step: '下一步（从哪里继续、做什么）',
  acceptance: '验收条件 / 测试证据', blocker: '阻塞原因与解阻条件',
};

export function NodeTodos({ todos = [], onChange, isPlaceholder, readOnly }) {
  return (
    <details className="node-notes-section" open={isPlaceholder || todos.length > 0}>
      <summary>TODO（{todos.filter((todo) => todo.status !== 'done').length} 未完成）</summary>
      {isPlaceholder && <p className="muted">仅为可运行骨架，不代表算法实现。请填写源码索引、下一步和验收条件。</p>}
      {readOnly && <p className="muted">公共子图定义只读；请在工程实例记录 TODO，或复制为工程私有定义后编辑。</p>}
      {todos.map((todo, index) => {
        const change = (key, value) => onChange(todos.map((item, i) => i === index ? { ...item, [key]: value } : item));
        return (
          <fieldset key={todo.id} className="todo-item" disabled={readOnly}>
            <legend>{todo.id}</legend>
            <label>状态
              <select value={todo.status} onChange={(event) => change('status', event.target.value)}>
                {Object.entries(STATUS).map(([value, name]) => <option key={value} value={value}>{name}</option>)}
              </select>
            </label>
            {Object.entries(FIELDS).map(([key, name]) => (
              <label key={key}>{name}
                <textarea rows={key === 'title' ? 1 : 2} value={todo[key] || ''}
                  onChange={(event) => change(key, event.target.value)} />
              </label>
            ))}
            <button onClick={() => onChange(todos.filter((_, i) => i !== index))}>删除此 TODO</button>
          </fieldset>
        );
      })}
      <button disabled={readOnly} onClick={() => onChange([...todos, newTodo(todos)])}>＋ 添加 TODO</button>
    </details>
  );
}

export function PlaceholderPortsWidget({ schema, value, onChange, disabled }) {
  const [errorText, setErrorText] = useState('');
  let ports;
  try {
    ports = placeholderPorts(value ?? DEFAULT_INTERFACE);
  } catch (error) {
    return <p role="alert">{error.message}；请在工程 YAML 中修正接口。</p>;
  }
  const save = (next) => onChange(schema.id, JSON.stringify(next.map(({ id, direction, channels }) => ({ id, direction, channels }))));
  const add = (direction) => {
    let index = 0;
    const prefix = direction === 'input' ? 'in' : 'out';
    while (ports.some((port) => port.id === `${prefix}${index}`)) index += 1;
    save([...ports, { id: `${prefix}${index}`, direction, channels: 2 }]);
  };
  return (
    <div className="placeholder-ports">
      <p className="muted">同 Task、f32 音频；各端口独立通道数。修改接口后请同步检查输出路由。</p>
      {ports.map((port, index) => (
        <div key={index} className="placeholder-port-row">
          <span>{port.direction === 'input' ? '输入' : '输出'}</span>
          <input aria-label={`端口 ${index} 名称`} value={port.id} disabled={disabled}
            onChange={(event) => {
              const id = event.target.value;
              if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(id) && !ports.some((p, i) => i !== index && p.id === id)) {
                setErrorText('');
                save(ports.map((p, i) => i === index ? { ...p, id } : p));
              } else {
                setErrorText('端口名须唯一，以字母/下划线开头，后接字母、数字或下划线');
              }
            }} />
          <input aria-label={`端口 ${index} 通道数`} type="number" min={1} max={64} value={port.channels} disabled={disabled}
            onChange={(event) => {
              const channels = Number(event.target.value);
              if (Number.isInteger(channels) && channels >= 1 && channels <= 64) {
                setErrorText('');
                save(ports.map((p, i) => i === index ? { ...p, channels } : p));
              } else {
                setErrorText('通道数必须是 1 至 64 的整数');
              }
            }} />
          <button disabled={disabled || ports.length === 1} onClick={() => save(ports.filter((_, i) => i !== index))}>×</button>
        </div>
      ))}
      {errorText && <p role="alert">{errorText}</p>}
      <button disabled={disabled || ports.filter((p) => p.direction === 'input').length >= 32} onClick={() => add('input')}>＋ 输入</button>
      <button disabled={disabled || ports.filter((p) => p.direction === 'output').length >= 32} onClick={() => add('output')}>＋ 输出</button>
    </div>
  );
}

export default function TodoPanel({ views, onLocate }) {
  const [includeDone, setIncludeDone] = useState(false);
  const items = collectViewTodos(views, includeDone);
  return (
    <div className="sidebar">
      <h3>工程 TODO</h3>
      <p className="muted">主图及全部子图定义（含未实例化定义）；共享定义只列一次，包含尚未保存的编辑。</p>
      <label><input type="checkbox" checked={includeDone} onChange={(event) => setIncludeDone(event.target.checked)} /> 显示已完成</label>
      <p>未完成 {items.filter((todo) => todo.status !== 'done').length} / 当前显示 {items.length}</p>
      {items.map((todo) => (
        <div key={todo.location} className="todo-item">
          <button onClick={() => onLocate(todo.view, todo.node)}>{todo.location}</button>
          <strong>{STATUS[todo.status]}：{todo.title}</strong>
          {todo.source_ref && <p>索引：{todo.source_ref}</p>}
          {todo.next_step && <p>下一步：{todo.next_step}</p>}
          {todo.blocker && <p role="alert">阻塞：{todo.blocker}</p>}
          {todo.synthetic && <p className="muted">自动发现的未实现项；请选择节点补充结构化 TODO。</p>}
        </div>
      ))}
      {!items.length && <p>当前范围无 TODO。</p>}
    </div>
  );
}

export const isRunnablePlaceholder = (node) => node?.data.component === PLACEHOLDER_ID;
