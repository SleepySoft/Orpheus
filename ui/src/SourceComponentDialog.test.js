import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import SourceComponentDialog, { INITIAL_CONFIGURATION, TRIGGER_CONFIGURATION } from './SourceComponentDialog';
import * as api from './api';

jest.mock('./api', () => ({
  saveSourceComponent: jest.fn(), runSourceComponent: jest.fn(), readSourceComponent: jest.fn(),
  getEditToken: jest.fn(() => 'lease'),
  errorDetail: (error) => error.message,
}));

let container;
let root;
let registered;
beforeEach(() => {
  jest.clearAllMocks();
  api.getEditToken.mockReturnValue('lease');
  global.IS_REACT_ACT_ENVIRONMENT = true;
  container = document.createElement('div');
  document.body.appendChild(container);
  root = createRoot(container);
  registered = jest.fn().mockResolvedValue(undefined);
  api.saveSourceComponent.mockResolvedValue({ sha256: 'saved' });
  api.runSourceComponent.mockResolvedValue({
    ok: true, registered: true, component: { name: '自建音效' }, imports: ['components/my_effect/component.yaml'],
  });
});
afterEach(() => {
  act(() => root.unmount());
  container.remove();
});
const render = (props = {}) => act(() => root.render(
  <SourceComponentDialog project="demo" onRegistered={registered} onClose={jest.fn()} {...props} />
));
const button = (text) => Array.from(container.querySelectorAll('button'))
  .find((element) => element.textContent === text);
const click = async (text) => act(async () => button(text).click());
const edit = (selector, value) => act(() => {
  const element = container.querySelector(selector);
  const prototype = element.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
  Object.getOwnPropertyDescriptor(prototype, 'value').set.call(element, value);
  element.dispatchEvent(new Event('input', { bubbles: true }));
});

test('先保存用户编辑的配置，再按摘要运行同一脚本服务，成功刷新注册组件', async () => {
  render();
  expect(container.textContent).toContain('demo/components/my_effect/component.yaml');
  edit('textarea', `${INITIAL_CONFIGURATION}# 用户修订\n`);
  await click('保存并生成');
  expect(api.saveSourceComponent).toHaveBeenCalledWith(
    'demo', 'my_effect', `${INITIAL_CONFIGURATION}# 用户修订\n`, null, 'lease');
  expect(api.runSourceComponent).toHaveBeenCalledWith('demo', 'my_effect', 'generate', 'saved', 'lease');
  expect(api.saveSourceComponent.mock.invocationCallOrder[0])
    .toBeLessThan(api.runSourceComponent.mock.invocationCallOrder[0]);
  expect(registered).toHaveBeenCalledTimes(1);
  expect(container.textContent).toContain('可从组件库拖入画布');
});

test('保存失败不生成；生成失败保留文本和摘要供修订，不宣称注册成功', async () => {
  render();
  api.saveSourceComponent.mockRejectedValueOnce(new Error('磁盘只读'));
  await click('保存并生成');
  expect(api.runSourceComponent).not.toHaveBeenCalled();
  expect(container.textContent).toContain('配置保存或载入失败');
  api.runSourceComponent.mockResolvedValueOnce({
    ok: false, registered: false, phase: 'generation',
    diagnostics: [{ location: 'generated', message: '生成区被修改' }],
  });
  await click('保存并生成');
  expect(registered).not.toHaveBeenCalled();
  expect(container.textContent).toContain('配置已保存；生成失败');
  expect(container.querySelector('textarea').value).toBe(INITIAL_CONFIGURATION);
  edit('textarea', '# 修订');
  await click('保存配置');
  expect(api.saveSourceComponent).toHaveBeenLastCalledWith('demo', 'my_effect', '# 修订', 'saved', 'lease');
});

test('保存与只读校验不会注册，已有配置可以载入继续修改', async () => {
  render();
  api.readSourceComponent.mockResolvedValue({ configuration: '# 已有配置', sha256: 'existing' });
  await click('载入已有配置');
  expect(container.querySelector('textarea').value).toBe('# 已有配置');
  api.runSourceComponent.mockResolvedValueOnce({ ok: true, registered: false });
  await click('保存并校验');
  expect(api.saveSourceComponent).toHaveBeenCalledWith('demo', 'my_effect', '# 已有配置', 'existing', 'lease');
  expect(api.runSourceComponent).toHaveBeenCalledWith('demo', 'my_effect', 'validate', 'saved', 'lease');
  expect(registered).not.toHaveBeenCalled();
  expect(container.textContent).toContain('尚未生成或注册');
});

test('包名和编辑租约限制生效', async () => {
  render();
  edit('input', '../outside');
  expect(button('保存并生成').disabled).toBe(true);
  edit('input', 'con');
  expect(button('保存配置').disabled).toBe(true);
  render({ readOnly: true });
  expect(container.querySelector('textarea').disabled).toBe(true);
  expect(button('载入已有配置').disabled).toBe(true);
  expect(container.textContent).toContain('当前没有编辑权');
});

test('纯触发接口模板不伪造PCM字段，保存使用同一配置驱动服务', async () => {
  render();
  await click('填入纯触发示例');
  expect(container.querySelector('textarea').value).toBe(TRIGGER_CONFIGURATION);
  expect(TRIGGER_CONFIGURATION).toContain('type: trigger');
  expect(TRIGGER_CONFIGURATION).toContain('drive_port_no: 1');
  expect(TRIGGER_CONFIGURATION).not.toMatch(/channels|sample_format|block_size|drv:/);
  expect(container.textContent).toContain('trigger 不携带 PCM');
  await click('保存并生成');
  expect(api.saveSourceComponent).toHaveBeenCalledWith(
    'demo', 'my_effect', TRIGGER_CONFIGURATION, null, 'lease');
});
