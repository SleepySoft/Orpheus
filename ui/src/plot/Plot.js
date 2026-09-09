import React from 'react';
import { useViewport } from 'reactflow';
import { usePlotCanvasSize } from './usePlotCanvasSize';
import { axisTicks, formatTick, projectValue, resolveDomain, seriesValues } from './plotScales';

const PALETTE = ['#4cc9f0', '#f9c74f', '#90be6d', '#f94144', '#9d4edd'];
const MARGINS = {
  node: { left: 34, right: 8, top: 8, bottom: 17 },
  panel: { left: 48, right: 12, top: 14, bottom: 28 },
  expanded: { left: 56, right: 18, top: 18, bottom: 34 },
};

/** 遍历全部 series，合并出当前轴的数据范围。 */
function combinedExtent(series, key) {
  let result = null;
  for (const item of series) {
    for (const value of seriesValues(item, key)) {
      if (!Number.isFinite(value)) continue;
      if (!result) result = [value, value];
      else {
        result[0] = Math.min(result[0], value);
        result[1] = Math.max(result[1], value);
      }
    }
  }
  return result;
}

/** 一条折线的路径绘制；NaN 会自然分段。 */
function drawLinePath(ctx, data, plot, domains, axes) {
  const count = Math.min(data.x.length, data.y.length);
  ctx.beginPath();
  let started = false;
  for (let index = 0; index < count; index++) {
    const px = projectValue(data.x[index], domains.x, plot.left, plot.right, axes.x);
    const py = projectValue(data.y[index], domains.y, plot.bottom, plot.top, axes.y);
    if (!Number.isFinite(px) || !Number.isFinite(py)) {
      started = false;
      continue;
    }
    if (!started) {
      ctx.moveTo(px, py);
      started = true;
    } else ctx.lineTo(px, py);
  }
  ctx.stroke();
}

/** 通用专业绘图组件：统一坐标轴、网格、高分屏渲染和基础曲线样式。 */
export default function Plot({
  series = [],
  x = {},
  y = {},
  variant = 'node',
  grid = 'major',
  legend = false,
  emptyText = '暂无数据',
  zoom,
}) {
  const viewport = useViewport();
  const effectiveZoom = zoom ?? viewport?.zoom ?? 1;
  const { wrapRef, width, height, pixelRatio } = usePlotCanvasSize(effectiveZoom);
  const canvasRef = React.useRef(null);

  React.useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || width < 2 || height < 2) return;
    canvas.width = Math.round(width * pixelRatio);
    canvas.height = Math.round(height * pixelRatio);
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = '#0d1117';
    ctx.fillRect(0, 0, width, height);

    const margin = MARGINS[variant] || MARGINS.node;
    const plot = {
      left: margin.left,
      right: Math.max(margin.left + 1, width - margin.right),
      top: margin.top,
      bottom: Math.max(margin.top + 1, height - margin.bottom),
    };
    const domains = {
      x: resolveDomain(x, combinedExtent(series, 'x')),
      y: resolveDomain(y, combinedExtent(series, 'y'), { pad: true }),
    };
    const tickCounts = variant === 'node' ? { x: 3, y: 3 } : { x: 5, y: 5 };
    const xTicks = axisTicks(x, domains.x, tickCounts.x);
    const yTicks = axisTicks(y, domains.y, tickCounts.y);

    ctx.lineWidth = 1;
    ctx.font = (variant === 'node' ? '9px ' : '10px ') + 'Inter, ui-sans-serif, system-ui, sans-serif';
    ctx.strokeStyle = 'rgba(255,255,255,0.075)';
    ctx.fillStyle = 'rgba(255,255,255,0.58)';

    if (grid !== 'none') {
      for (const tick of xTicks) {
        const px = projectValue(tick, domains.x, plot.left, plot.right, x);
        if (!Number.isFinite(px)) continue;
        ctx.beginPath();
        ctx.moveTo(px, plot.top);
        ctx.lineTo(px, plot.bottom);
        ctx.stroke();
      }
      for (const tick of yTicks) {
        const py = projectValue(tick, domains.y, plot.bottom, plot.top, y);
        if (!Number.isFinite(py)) continue;
        ctx.beginPath();
        ctx.moveTo(plot.left, py);
        ctx.lineTo(plot.right, py);
        ctx.stroke();
      }
    }

    ctx.strokeStyle = 'rgba(255,255,255,0.34)';
    ctx.beginPath();
    ctx.moveTo(plot.left, plot.bottom);
    ctx.lineTo(plot.right, plot.bottom);
    ctx.moveTo(plot.left, plot.top);
    ctx.lineTo(plot.left, plot.bottom);
    ctx.stroke();

    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    for (const tick of xTicks) {
      const px = projectValue(tick, domains.x, plot.left, plot.right, x);
      if (Number.isFinite(px)) ctx.fillText(formatTick(tick, x), px, plot.bottom + 4);
    }

    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (const tick of yTicks) {
      const py = projectValue(tick, domains.y, plot.bottom, plot.top, y);
      if (Number.isFinite(py)) ctx.fillText(formatTick(tick, y), plot.left - 4, py);
    }

    if (variant !== 'node' && (x.label || y.label)) {
      ctx.fillStyle = 'rgba(255,255,255,0.72)';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'bottom';
      ctx.fillText([x.label, x.unit].filter(Boolean).join(' · '), (plot.left + plot.right) / 2, height - 2);
      ctx.save();
      ctx.translate(10, (plot.top + plot.bottom) / 2);
      ctx.rotate(-Math.PI / 2);
      ctx.textBaseline = 'top';
      ctx.fillText([y.label, y.unit].filter(Boolean).join(' · '), 0, 0);
      ctx.restore();
    }

    if (!series.length) {
      ctx.fillStyle = 'rgba(255,255,255,0.48)';
      ctx.font = (variant === 'node' ? '10px ' : '12px ') + 'Inter, ui-sans-serif, system-ui, sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(emptyText, (plot.left + plot.right) / 2, (plot.top + plot.bottom) / 2);
      return;
    }

    series.forEach((item, index) => {
      const data = { x: seriesValues(item, 'x'), y: seriesValues(item, 'y') };
      if (!data.x.length || !data.y.length) return;
      const color = item.color || PALETTE[index % PALETTE.length];
      ctx.strokeStyle = color;
      ctx.fillStyle = item.fillColor || (color + '22');
      ctx.lineWidth = item.width || (variant === 'node' ? 1.2 : 1.6);
      ctx.globalAlpha = item.opacity ?? 1;
      if (item.type === 'bars') {
        const step = data.x.length > 1
          ? Math.abs(
            projectValue(data.x[1], domains.x, plot.left, plot.right, x)
              - projectValue(data.x[0], domains.x, plot.left, plot.right, x),
          )
          : plot.right - plot.left;
        const barWidth = Math.max(1, step * 0.72);
        for (let pointIndex = 0; pointIndex < Math.min(data.x.length, data.y.length); pointIndex++) {
          const px = projectValue(data.x[pointIndex], domains.x, plot.left, plot.right, x);
          const py = projectValue(data.y[pointIndex], domains.y, plot.bottom, plot.top, y);
          if (Number.isFinite(px) && Number.isFinite(py)) {
            ctx.fillRect(px - barWidth / 2, py, barWidth, plot.bottom - py);
          }
        }
      } else {
        drawLinePath(ctx, data, plot, domains, { x, y });
        if (item.type === 'area') {
          const firstX = projectValue(data.x[0], domains.x, plot.left, plot.right, x);
          const lastX = projectValue(data.x[data.x.length - 1], domains.x, plot.left, plot.right, x);
          ctx.lineTo(lastX, plot.bottom);
          ctx.lineTo(firstX, plot.bottom);
          ctx.closePath();
          ctx.fill();
        }
      }
      ctx.globalAlpha = 1;
    });
  }, [series, x, y, variant, grid, legend, emptyText, width, height, pixelRatio]);

  return (
    <div ref={wrapRef} className="plot-widget">
      <canvas
        ref={canvasRef}
        className="plot-canvas"
        style={{ width: '100%', height: '100%', display: 'block', borderRadius: 4 }}
      />
    </div>
  );
}
