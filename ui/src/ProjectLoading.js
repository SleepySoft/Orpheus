import React, { useEffect, useRef, useState } from 'react';

const STEPS = [
  ['bootstrap', '连接后端与读取工程列表'],
  ['fetch', '读取工程、组件与布局'],
  ['convert', '转换节点和视图'],
  ['render', '初始化画布'],
];

export default function ProjectLoading({ progress }) {
  const panel = useRef(null);
  const [now, setNow] = useState(Date.now);
  useEffect(() => {
    const previous = document.activeElement;
    panel.current?.focus();
    const timer = setInterval(() => setNow(Date.now()), 1000);
    return () => {
      clearInterval(timer);
      if (previous?.isConnected) previous.focus();
    };
  }, []);
  const stageIndex = STEPS.findIndex(([stage]) => stage === progress.stage);
  const elapsed = Math.max(0, Math.floor((now - progress.startedAt) / 1000));
  const converting = progress.stage === 'convert';
  return (
    <div className="project-loading-backdrop">
      <section
        ref={panel}
        className="project-loading"
        role="dialog"
        aria-modal="true"
        aria-labelledby="project-loading-title"
        tabIndex={-1}
        onKeyDown={(event) => {
          event.stopPropagation();
          if (event.key === 'Tab' || ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 's')) {
            event.preventDefault();
          }
        }}
      >
        <h2 id="project-loading-title"><span className="loading-spinner" />正在载入工程</h2>
        <div className="loading-project-name">{progress.name || '准备工程列表'}</div>
        <ol className="loading-steps">
          {STEPS.map(([stage, label], index) => (
            <li
              key={stage}
              className={index === stageIndex ? 'active' : index < stageIndex ? 'done' : ''}
              aria-current={index === stageIndex ? 'step' : undefined}
            >
              {index < stageIndex ? '✓ ' : `${index + 1}. `}{label}
            </li>
          ))}
        </ol>
        <progress
          aria-label="当前阶段载入进度"
          value={converting ? progress.completed : undefined}
          max={converting ? progress.total : undefined}
        />
        <p role="status">
          {converting
            ? `已转换 ${progress.completed}/${progress.total} 个视图 · ${progress.nodeCount} 个节点`
            : progress.stage === 'render'
              ? `正在测量并绘制 ${progress.nodeCount} 个主图节点`
              : `已读取 ${progress.completed}/${progress.total} 项资源，正在等待后端响应`}
        </p>
        <small aria-hidden="true">已用时 {elapsed} 秒；大模型首次载入可能需要较长时间</small>
      </section>
    </div>
  );
}
