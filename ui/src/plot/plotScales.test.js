import {
  axisTicks,
  formatTick,
  projectValue,
  resolveDomain,
  unprojectValue,
  waveformEnvelope,
} from './plotScales';

describe('plot 刻度与坐标映射', () => {
  test('线性刻度生成可读步长', () => {
    expect(axisTicks({}, [0, 100], 5)).toEqual([0, 20, 40, 60, 80, 100]);
    expect(axisTicks({}, [-1, 1], 3)).toEqual([-1, 0, 1]);
  });

  test('对数轴使用十进制与 2/5 刻度', () => {
    expect(axisTicks({ scale: 'log' }, [20, 20000], 5))
      .toEqual([20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]);
  });

  test('数据域支持显式覆盖和留白', () => {
    expect(resolveDomain({ domain: [10, 20] }, [0, 100])).toEqual([10, 20]);
    expect(resolveDomain({}, [0, 100], { pad: true })[0]).toBeLessThan(0);
  });

  test('值域映射为画布坐标', () => {
    expect(projectValue(0, [0, 10], 0, 100, {})).toBe(0);
    expect(projectValue(10, [0, 10], 0, 100, {})).toBe(100);
    expect(projectValue(100, [1, 1000], 0, 100, { scale: 'log' })).toBeCloseTo(66.6667);
  });

  test('画布坐标可以反解为数据值', () => {
    expect(unprojectValue(50, [0, 10], 0, 100, {})).toBe(5);
    expect(unprojectValue(50, [1, 1000], 0, 100, { scale: 'log' })).toBeCloseTo(Math.sqrt(1000));
  });

  test('频率刻度使用专业缩写', () => {
    expect(formatTick(1000, { unit: 'Hz' })).toBe('1k');
    expect(formatTick(10000, { unit: 'Hz' })).toBe('10k');
    expect(formatTick(-20, { unit: 'dB' })).toBe('-20');
  });

  test('波形按可视列压缩为 min/max 包络', () => {
    expect(waveformEnvelope([1, -1, 0.2, -0.2, 0, 0.5], 2)).toEqual([
      [-1, 1],
      [-0.2, 0.5],
    ]);
    expect(waveformEnvelope([], 4)).toEqual([]);
  });
});
