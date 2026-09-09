/** 将任意序列统一转换为有限数值数组；空位与非法值会被过滤。 */
function numericValues(values) {
  if (!values) return [];
  return Array.from(values).filter((value) => Number.isFinite(value));
}

/** 求数值序列的值域；无效输入返回 null。 */
export function extent(values) {
  const valid = numericValues(values);
  if (!valid.length) return null;
  return [Math.min(...valid), Math.max(...valid)];
}

/** 根据显式 domain 或数据推断绘图域；曲线通常需要少量留白。 */
export function resolveDomain(axis, values, options) {
  const pad = options && options.pad;
  let domain = null;
  if (Array.isArray(axis.domain) && axis.domain.length === 2) {
    const low = axis.domain[0];
    const high = axis.domain[1];
    if (Number.isFinite(low) && Number.isFinite(high) && high > low) domain = [low, high];
  }
  if (!domain) {
    const measured = extent(values);
    if (!measured) domain = axis.scale === 'log' ? [1, 10] : [0, 1];
    else domain = measured;
  }
  let low = domain[0];
  let high = domain[1];
  if (axis.scale === 'log' && (low <= 0 || high <= 0)) {
    low = 1;
    high = 10;
  }
  if (high === low) {
    const spread = axis.scale === 'log' ? Math.max(1, Math.abs(low) * 0.1) : 1;
    low -= spread;
    high += spread;
  }
  if (pad && axis.scale !== 'log') {
    const margin = (high - low) * 0.06;
    low -= margin;
    high += margin;
  }
  return [low, high];
}

/** 生成 1/2/5 步长的可读刻度，适合线性时间和幅值轴。 */
function linearTicks(domain, targetCount) {
  const low = domain[0];
  const high = domain[1];
  const span = high - low;
  if (!(span > 0) || !(targetCount > 0)) return [];
  const roughStep = span / targetCount;
  const magnitude = 10 ** Math.floor(Math.log10(roughStep));
  const relative = roughStep / magnitude;
  const step = (relative <= 1 ? 1 : relative <= 2 ? 2 : relative <= 5 ? 5 : 10) * magnitude;
  const start = Math.ceil(low / step) * step;
  const ticks = [];
  for (let value = start; value <= high + step * 1e-6; value += step) {
    ticks.push(Number(value.toFixed(12)));
  }
  return ticks;
}

/** 对数轴用十进位和 2/5 刻度，兼顾可读性与专业感。 */
function logTicks(domain, targetCount) {
  const safeLow = Math.max(1e-12, domain[0]);
  const safeHigh = Math.max(safeLow, domain[1]);
  const decades = Math.log10(safeHigh / safeLow);
  const candidates = [];
  const startDecade = Math.floor(Math.log10(safeLow));
  const endDecade = Math.ceil(Math.log10(safeHigh));
  const useFineTicks = decades <= 3;
  for (let decade = startDecade; decade <= endDecade; decade++) {
    const multipliers = useFineTicks ? [1, 2, 5] : [1];
    for (const multiplier of multipliers) {
      const value = multiplier * 10 ** decade;
      if (value >= safeLow * 0.999 && value <= safeHigh * 1.001) candidates.push(value);
    }
  }
  if (!candidates.length) return [safeLow, safeHigh];
  return candidates;
}

/** 为指定轴生成刻度；显式 ticks 优先。 */
export function axisTicks(axis, domain, targetCount) {
  if (Array.isArray(axis.ticks)) return axis.ticks.filter((value) => Number.isFinite(value));
  if (Number.isFinite(axis.ticks)) return axis.ticks;
  return axis.scale === 'log'
    ? logTicks(domain, targetCount)
    : linearTicks(domain, targetCount);
}

/** 将数据值映射到画布坐标；对数轴使用 log10 线性插值。 */
export function projectValue(value, domain, start, end, axis) {
  if (!Number.isFinite(value)) return NaN;
  if (axis.scale === 'log') {
    const safeLow = Math.log10(Math.max(1e-12, domain[0]));
    const safeHigh = Math.log10(Math.max(safeLow, domain[1]));
    const safeValue = Math.log10(Math.max(1e-12, value));
    return start + ((safeValue - safeLow) / (safeHigh - safeLow)) * (end - start);
  }
  return start + ((value - domain[0]) / (domain[1] - domain[0])) * (end - start);
}

/** 频率用 Hz/kHz/MHz，dB 保留整数，其余自动选择紧凑格式。 */
export function formatTick(value, axis) {
  const effectiveAxis = axis || {};
  if (!Number.isFinite(value)) return '';
  if (effectiveAxis.unit === 'Hz') {
    const abs = Math.abs(value);
    if (abs >= 1e6) return String(value / 1e6) + 'M';
    if (abs >= 1e3) return String(value / 1e3) + 'k';
    return String(Math.round(value));
  }
  if (effectiveAxis.unit === 'dB') return String(Math.round(value));
  if (Math.abs(value) >= 1000) return value.toFixed(0);
  if (Math.abs(value) >= 10) return value.toFixed(1);
  if (Math.abs(value) >= 1) return value.toFixed(2);
  return value.toFixed(3).replace(/0+$/, '').replace(/\.$/, '');
}

/** 从 series 中读取 x/y 数据；兼容普通数组和 TypedArray。 */
export function seriesValues(series, key) {
  if (!series) return [];
  if (series[key] != null) return series[key];
  if (Array.isArray(series.points)) return series.points.map((point) => point[key]);
  return [];
}
