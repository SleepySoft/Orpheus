import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import AudioEdge from './AudioEdge';

jest.mock('reactflow', () => ({
  getBezierPath: () => ['M0,0 L20,20'],
  BaseEdge: ({ id, path, style }) => <path id={id} d={path} style={style} />,
}));

test('失配连线标红并解释两端配置，修正后恢复，不因选中隐藏红色', () => {
  global.IS_REACT_ACT_ENVIRONMENT = true;
  const container = document.createElement('div');
  const root = createRoot(container);
  const data = { status: 'mismatch', from: 'a:out', to: 'b:in',
    differences: [{ field: 'channels', source: 2, target: 1 }] };
  try {
    act(() => root.render(<svg><AudioEdge id="edge" selected data={data} /></svg>));
    expect(container.querySelector('.audio-edge-path.mismatch')).not.toBeNull();
    expect(container.querySelector('path').style.stroke).toBe('#e03131');
    expect(container.querySelector('title').textContent).toContain('通道数：a:out（2） → b:in（1）');
    expect(container.textContent).toContain('请手动修改');
    act(() => root.render(<svg><AudioEdge id="edge" data={{ status: 'matched' }} /></svg>));
    expect(container.querySelector('.mismatch')).toBeNull();
    expect(container.querySelector('path').style.stroke).toBe('');
  } finally {
    act(() => root.unmount());
  }
});
