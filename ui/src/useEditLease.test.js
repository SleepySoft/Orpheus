import React, { act, StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import useEditLease from './useEditLease';
import * as api from './api';

jest.mock('./api', () => ({
  acquireEditing: jest.fn(), renewEditing: jest.fn(), releaseEditing: jest.fn(),
  getEditToken: jest.fn(), setEditToken: jest.fn(), onEditLeaseLost: jest.fn(),
  errorDetail: (error) => error.message,
}));

let root, container, latest, token, deadline, lostListener;
function Editor() {
  latest = useEditLease();
  return null;
}
beforeEach(() => {
  global.IS_REACT_ACT_ENVIRONMENT = true;
  jest.useFakeTimers();
  jest.clearAllMocks();
  token = null;
  api.setEditToken.mockImplementation((value, seconds = 30) => {
    token = value;
    deadline = Date.now() + seconds * 1000;
  });
  api.getEditToken.mockImplementation(() => Date.now() < deadline ? token : null);
  api.acquireEditing.mockResolvedValue({ token: 'first', expires_in: 30 });
  api.renewEditing.mockResolvedValue({ expires_in: 30 });
  api.releaseEditing.mockResolvedValue({});
  api.onEditLeaseLost.mockImplementation((listener) => {
    lostListener = listener;
    return () => {};
  });
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
});
afterEach(() => {
  act(() => root.unmount());
  container.remove();
  jest.useRealTimers();
});
const mount = async () => act(async () => { root.render(<StrictMode><Editor /></StrictMode>); });

test('StrictMode 只申请一次，续租不会重新抢权', async () => {
  await mount();
  expect(api.acquireEditing).toHaveBeenCalledTimes(1);
  expect(latest.canEdit).toBe(true);
  await act(async () => { jest.advanceTimersByTime(10000); });
  expect(api.renewEditing).toHaveBeenCalledWith('first');
});

test('续租失败立即只读，保留状态且不自动抢回', async () => {
  await mount();
  api.renewEditing.mockRejectedValue(new Error('被接管'));
  await act(async () => { jest.advanceTimersByTime(10000); });
  expect(latest.canEdit).toBe(false);
  expect(latest.canEditRef.current).toBe(false);
  expect(latest.message).toContain('未保存内容仍保留');
  await act(async () => { jest.advanceTimersByTime(60000); });
  expect(api.acquireEditing).toHaveBeenCalledTimes(1);
});

test('接管期间只读，重新加载成功后才启用，长载入也持续续租', async () => {
  api.acquireEditing.mockRejectedValueOnce(new Error('其他页面编辑'));
  await mount();
  api.acquireEditing.mockResolvedValue({ token: 'second', expires_in: 30 });
  let finish;
  const reload = jest.fn(() => new Promise((resolve) => { finish = resolve; }));
  let taking;
  await act(async () => { taking = latest.takeover(reload); });
  expect(latest.canEdit).toBe(false);
  expect(reload).toHaveBeenCalledTimes(1);
  for (let i = 0; i < 4; i++) {
    await act(async () => { jest.advanceTimersByTime(10000); });
  }
  expect(api.renewEditing).toHaveBeenLastCalledWith('second');
  await act(async () => { finish(); await taking; });
  expect(latest.canEdit).toBe(true);
});

test('接管加载失败释放租约，不能在旧快照上继续写', async () => {
  await mount();
  api.acquireEditing.mockResolvedValue({ token: 'second', expires_in: 30 });
  await act(async () => {
    await expect(latest.takeover(async () => { throw new Error('载入失败'); })).rejects.toThrow('载入失败');
  });
  expect(latest.canEdit).toBe(false);
  expect(api.getEditToken()).toBeNull();
  expect(api.releaseEditing).toHaveBeenCalledWith('second');
});

test('后台恢复时发现本地租约到期立即只读；关闭以 keepalive 释放', async () => {
  await mount();
  act(() => {
    api.setEditToken(null);
    window.dispatchEvent(new Event('focus'));
  });
  expect(latest.canEdit).toBe(false);
  expect(latest.message).toContain('已过期');
});

test('旧页面失权后迟到的续租成功不能恢复写权限', async () => {
  await mount();
  let finish;
  api.renewEditing.mockImplementation(() => new Promise((resolve) => { finish = resolve; }));
  await act(async () => { jest.advanceTimersByTime(10000); });
  act(() => lostListener('已被其他页面接管'));
  await act(async () => { finish({ expires_in: 30 }); });
  expect(latest.canEdit).toBe(false);
  expect(api.getEditToken()).toBeNull();
});

test('离开页面主动释放，恢复后不自动抢权', async () => {
  await mount();
  act(() => window.dispatchEvent(new Event('pagehide')));
  expect(api.releaseEditing).toHaveBeenCalledWith('first', true);
  expect(latest.canEdit).toBe(false);
});
