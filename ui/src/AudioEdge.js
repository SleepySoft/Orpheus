import React from 'react';
import { BaseEdge, getBezierPath } from 'reactflow';

/** Audio signal edge: solid cool-colored path, visually distinct from control links. */
export default function AudioEdge({
  id,
  sourceX,
  sourceY,
  targetX,
  targetY,
  sourcePosition,
  targetPosition,
  data,
  selected,
}) {
  const [edgePath] = getBezierPath({
    sourceX,
    sourceY,
    sourcePosition,
    targetX,
    targetY,
    targetPosition,
  });
  const invalid = data?.status === 'mismatch' || data?.status === 'error';
  const labels = {
    type: '数据类型', sample_format: '样本格式', channels: '通道数',
    frames: '块长（交付）', sample_rate: '采样率', layout: '布局', frame_size: '帧结构',
  };
  const differences = (data?.differences || []).map((item) =>
    `${labels[item.field] || item.field}：${data.from}（${item.source ?? '未声明'}） → `
    + `${data.to}（${item.target ?? '未声明'}）`).join('\n');
  const title = data?.status === 'error' ? `连线解析失败：${data.message}`
    : invalid ? `连线配置不匹配，请手动修改；配置和连线均保留。\n${differences || data.message}`
    : data?.status === 'matched' ? '两端端口规格匹配' : '等待后端连线校验';
  return (
    <g className={`audio-edge-path${invalid ? ' mismatch' : ''}`}
      data-connection-status={data?.status || 'pending'}>
      <title>{title}</title>
      <BaseEdge
        id={id}
        path={edgePath}
        style={{ strokeWidth: selected ? 3 : 2.2, ...(invalid ? { stroke: '#e03131' } : {}) }}
      />
    </g>
  );
}
