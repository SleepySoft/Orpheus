import React from 'react';
import { useViewport } from 'reactflow';
import { usePlotCanvasSize } from './usePlotCanvasSize';
import {
  axisTicks,
  formatTick,
  projectValue,
  resolveDomain,
  seriesValues,
  unprojectValue,
  waveformEnvelope,
} from './plotScales';

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
    const values = item.type === 'waveform' ? item.data : seriesValues(item, key);
    for (const value of values) {
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

/** 绘制填充式波形包络：每列保留 min/max，避免逐样本绘制造成的性能瓶颈。 */
function drawWaveform(ctx, item, plot, domains, axes) {
  const columns = Math.max(2, Math.round(plot.right - plot.left));
  const envelope = waveformEnvelope(item.data, columns);
  if (!envelope.length) return;
  const columnWidth = (plot.right - plot.left) / (columns - 1);
  const pointAt = (column, value) => ({
    x: plot.left + column * columnWidth,
    y: projectValue(value, domains.y, plot.bottom, plot.top, axes.y),
  });

  ctx.beginPath();
  for (let column = 0; column < columns; column++) {
    if (!envelope[column]) continue;
    const top = pointAt(column, envelope[column][1]);
    if (!Number.isFinite(top.y)) continue;
    if (column === 0) ctx.moveTo(top.x, top.y);
    else ctx.lineTo(top.x, top.y);
  }
  for (let column = columns - 1; column >= 0; column--) {
    if (!envelope[column]) continue;
    const bottom = pointAt(column, envelope[column][0]);
    if (Number.isFinite(bottom.y)) ctx.lineTo(bottom.x, bottom.y);
  }
  ctx.closePath();
  ctx.fillStyle = item.fillColor || ((item.color || PALETTE[0]) + '22');
  ctx.fill();
  ctx.stroke();
}

/** 绘制方阵热力图；低相干偏青，高相干偏红。 */
function drawHeatmap(ctx, item, plot) {
  const size = item.n || 0;
  const matrix = item.matrix;
  if (!Number.isInteger(size) || size <= 0 || !Array.isArray(matrix) || matrix.length !== size * size) {
    return;
  }
  const width = (plot.right - plot.left) / size;
  const height = (plot.bottom - plot.top) / size;
  for (let row = 0; row < size; row++) {
    for (let column = 0; column < size; column++) {
      const value = Math.max(0, Math.min(1, matrix[row * size + column] || 0));
      const red = Math.round(255 * value);
      const green = Math.round(255 * (1 - value));
      const blue = Math.round(255 * (1 - value));
      ctx.fillStyle = `rgb(${red},${green},${blue})`;
      ctx.fillRect(
        plot.left + column * width,
        plot.top + row * height,
        width + 0.5,
        height + 0.5,
      );
    }
  }
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
  crosshair = variant !== 'node',
  emptyText = '暂无数据',
  zoom,
}) {
  const viewport = useViewport();
  const effectiveZoom = zoom ?? viewport?.zoom ?? 1;
  const { wrapRef, width, height, pixelRatio } = usePlotCanvasSize(effectiveZoom);
  const canvasRef = React.useRef(null);
  const [hover, setHover] = React.useState(null);

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

    if (legend && series.some((item) => item.label && item.type !== 'heatmap')) {
      const entries = series
        .filter((item) => item.label && item.type !== 'heatmap')
        .slice(0, 3);
      ctx.font = (variant === 'node' ? '9px ' : '10px ') + 'Inter, ui-sans-serif, system-ui, sans-serif';
      let cursor = plot.right - 4;
      for (let index = entries.length - 1; index >= 0; index--) {
        const item = entries[index];
        const textWidth = ctx.measureText(item.label).width;
        cursor -= textWidth;
        ctx.fillStyle = 'rgba(255,255,255,0.78)';
        ctx.textAlign = 'left';
        ctx.textBaseline = 'middle';
        ctx.fillText(item.label, cursor, plot.top + 8);
        cursor -= 10;
        ctx.fillStyle = item.color || PALETTE[index % PALETTE.length];
        ctx.fillRect(cursor, plot.top + 6, 6, 4);
        cursor -= 8;
      }
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
      if (item.type !== 'waveform' && item.type !== 'heatmap' && (!data.x.length || !data.y.length)) return;
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
      } else if (item.type === 'waveform') {
        drawWaveform(ctx, item, plot, domains, { x, y });
      } else if (item.type === 'heatmap') {
        drawHeatmap(ctx, item, plot);
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

    if (crosshair && hover) {
      const inside = hover.x >= plot.left && hover.x <= plot.right
        && hover.y >= plot.top && hover.y <= plot.bottom;
      if (inside) {
        const xValue = unprojectValue(hover.x, domains.x, plot.left, plot.right, x);
        const yValue = unprojectValue(hover.y, domains.y, plot.bottom, plot.top, y);
        ctx.strokeStyle = 'rgba(255,255,255,0.38)';
        ctx.lineWidth = 1;
        ctx.setLineDash([3, 3]);
        ctx.beginPath();
        ctx.moveTo(hover.x, plot.top);
        ctx.lineTo(hover.x, plot.bottom);
        ctx.moveTo(plot.left, hover.y);
        ctx.lineTo(plot.right, hover.y);
        ctx.stroke();
        ctx.setLineDash([]);

        const xText = formatTick(xValue, x) + (x.unit ? ' ' + x.unit : '');
        const yText = formatTick(yValue, y) + (y.unit ? ' ' + y.unit : '');
        const text = xText + ', ' + yText;
        const textWidth = ctx.measureText(text).width;
        const boxX = Math.min(Math.max(hover.x + 8, plot.left + 2), plot.right - textWidth - 12);
        const boxY = Math.min(Math.max(hover.y - 22, plot.top + 2), plot.bottom - 18);
        ctx.fillStyle = 'rgba(13,17,23,0.88)';
        ctx.fillRect(boxX, boxY, textWidth + 10, 16);
        ctx.strokeStyle = 'rgba(255,255,255,0.18)';
        ctx.strokeRect(boxX, boxY, textWidth + 10, 16);
        ctx.fillStyle = 'rgba(255,255,255,0.88)';
        ctx.textAlign = 'left';
        ctx.textBaseline = 'middle';
        ctx.fillText(text, boxX + 5, boxY + 8);
      }
    }
  }, [series, x, y, variant, grid, legend, crosshair, emptyText, width, height, pixelRatio, hover]);

  return (
    <div ref={wrapRef} className="plot-widget">
      <canvas
        ref={canvasRef}
        className="plot-canvas"
        style={{ width: '100%', height: '100%', display: 'block', borderRadius: 4 }}
        onPointerMove={(event) => {
          const rect = event.currentTarget.getBoundingClientRect();
          setHover({ x: event.clientX - rect.left, y: event.clientY - rect.top });
        }}
        onPointerLeave={() => setHover(null)}
      />
    </div>
  );
}
