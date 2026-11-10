import React, { useEffect, useState } from 'react';
import { createPortal } from 'react-dom';

export default function ParameterWorkspace({ open, onOpenChange, children, readOnly = false }) {
  const [popup, setPopup] = useState(null);
  const [error, setError] = useState('');

  const restore = () => {
    setPopup(null);
    onOpenChange(true);
  };

  useEffect(() => {
    if (!popup) return undefined;
    const onClose = () => {
      setPopup(null);
      onOpenChange(true);
    };
    const close = () => {
      popup.removeEventListener('beforeunload', onClose);
      if (!popup.closed) popup.close();
    };
    popup.addEventListener('beforeunload', onClose);
    window.addEventListener('pagehide', close);
    return () => {
      window.removeEventListener('pagehide', close);
      close();
    };
  }, [popup, onOpenChange]);

  const detach = () => {
    const target = window.open('', '_blank', 'popup,width=1100,height=850,resizable=yes,scrollbars=yes');
    if (!target) {
      setError('独立窗口被浏览器拦截，请允许本站弹出窗口后重试。');
      return;
    }
    target.document.close();
    target.document.title = 'Orpheus · 参数面板';
    target.document.documentElement.lang = 'zh-CN';
    target.document.body.classList.add('parameter-window');
    document.querySelectorAll('style, link[rel="stylesheet"]').forEach((source) => {
      const sheet = source.sheet;
      if (sheet && (!sheet.href || new URL(sheet.href).origin === window.location.origin)) {
        // 复用已加载的同源样式，避免弹窗重新请求期间失去布局。
        const style = target.document.createElement('style');
        style.textContent = [...sheet.cssRules].map((rule) => rule.cssText).join('\n');
        style.media = source.media || '';
        target.document.head.appendChild(style);
        return;
      }
      const copy = source.cloneNode(true);
      if (source.tagName === 'LINK') copy.href = source.href;
      target.document.head.appendChild(copy);
    });
    setError('');
    setPopup(target);
    onOpenChange(false);
    target.focus();
  };

  const panel = (
    <section className="rightbar" aria-label="参数配置">
      <div className="side-panel-header">
        <span>参数 / 子组件</span>
        <div className="parameter-panel-actions">
          <button className="side-collapse" onClick={popup ? restore : detach}>
            {popup ? '收回侧栏' : '独立窗口'}
          </button>
          {!popup && <button className="side-collapse" onClick={() => onOpenChange(false)}
            title="收起参数面板">收起 »</button>}
        </div>
      </div>
      {error && <p className="parameter-window-error" role="alert">{error}</p>}
      {readOnly && <p className="muted">当前页面只读；未保存内容仍保留，可复制后再接管。</p>}
      <fieldset className="edit-controls" disabled={readOnly}>{children}</fieldset>
    </section>
  );

  if (popup) {
    return (
      <>
        {open && <div className="rightbar parameter-window-placeholder">
          <p>参数面板已在独立窗口中打开。</p>
          <button className="side-collapse" onClick={() => popup.focus()}>聚焦窗口</button>
          <button className="side-collapse" onClick={restore}>收回侧栏</button>
        </div>}
        {createPortal(panel, popup.document.body)}
      </>
    );
  }
  return <div className="parameter-workspace" hidden={!open}>{panel}</div>;
}
