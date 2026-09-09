import React from 'react';

/**
 * 观察容器的 CSS 尺寸，并结合高分屏与 React Flow zoom 计算实际像素。
 * canvas 的 CSS 尺寸仍保持 100%，避免布局尺寸抖动。
 */
export function usePlotCanvasSize(zoom = 1) {
  const wrapRef = React.useRef(null);
  const [size, setSize] = React.useState({ width: 0, height: 0 });
  const effectiveZoom = Number.isFinite(zoom) && zoom > 0 ? zoom : 1;

  React.useEffect(() => {
    const element = wrapRef.current;
    if (!element) return undefined;
    const observer = new ResizeObserver((entries) => {
      const rect = entries[0].contentRect;
      setSize((previous) => {
        const width = Math.max(1, Math.round(rect.width));
        const height = Math.max(1, Math.round(rect.height));
        return previous.width === width && previous.height === height ? previous : { width, height };
      });
    });
    observer.observe(element);
    return () => observer.disconnect();
  }, []);

  const devicePixelRatio = typeof window === 'undefined' ? 1 : window.devicePixelRatio || 1;
  const pixelRatio = Math.min(6, Math.max(1, devicePixelRatio * effectiveZoom));

  return { wrapRef, width: size.width, height: size.height, pixelRatio };
}
