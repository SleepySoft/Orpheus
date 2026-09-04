import { createContext } from 'react';

/** Context used by React Flow nodes to trigger app-level actions
 *  (e.g., opening the component README modal) without passing callbacks
 *  through the serializable node data. */
export const NodeActionsContext = createContext({
  showReadme: () => {},
  showControlLinks: false, // 「控制链路」显示开关：关闭时节点不渲染控制 handle
  revealSubExport: () => {}, // 点击子组件内部导出端点：定位对应接口条目
});
