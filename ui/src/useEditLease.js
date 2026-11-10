import { useCallback, useEffect, useRef, useState } from 'react';
import * as api from './api';

export default function useEditLease() {
  const [state, setState] = useState({ status: 'connecting', message: '正在获取编辑权…' });
  const [heldToken, setHeldToken] = useState(null);
  const initial = useRef(null);
  const canEditRef = useRef(false);
  const lose = useCallback((message) => {
    api.setEditToken(null);
    setHeldToken(null);
    canEditRef.current = false;
    setState({ status: 'readonly', message });
  }, []);
  const granted = useCallback((result) => {
    api.setEditToken(result.token, result.expires_in);
    setHeldToken(result.token);
    canEditRef.current = true;
    setState({ status: 'editing', message: '当前页面持有工作区编辑权' });
  }, []);

  useEffect(() => {
    let active = true;
    // StrictMode 重挂载复用同一次申请，不能让第二次申请把本页误判为旁观页。
    if (!initial.current) initial.current = api.acquireEditing();
    initial.current.then((result) => {
      if (active) granted(result);
    }).catch((error) => {
      if (active) lose(api.errorDetail(error));
    });
    return () => { active = false; };
  }, [granted, lose]);

  useEffect(() => api.onEditLeaseLost(lose), [lose]);

  useEffect(() => {
    if (!heldToken) return undefined;
    let active = true;
    let renewing = false;
    const token = heldToken;
    const check = () => {
      if (active && !api.getEditToken()) lose('编辑权已过期，未保存内容仍保留，请接管并刷新。');
    };
    const timer = setInterval(async () => {
      check();
      if (!api.getEditToken() || renewing) return;
      renewing = true;
      try {
        const result = await api.renewEditing(token);
        if (active) api.setEditToken(token, result.expires_in);
      } catch (error) {
        if (active) lose(`编辑权已失效或连接中断：${api.errorDetail(error)}。未保存内容仍保留。`);
      } finally {
        renewing = false;
      }
    }, 10000);
    const leave = () => {
      lose('页面已离开，返回后请接管并刷新。');
      api.releaseEditing(token, true).catch((error) => console.warn('释放编辑权失败，将等待租约过期', error));
    };
    window.addEventListener('focus', check);
    window.addEventListener('pointerdown', check, true);
    window.addEventListener('keydown', check, true);
    window.addEventListener('pagehide', leave);
    return () => {
      active = false;
      clearInterval(timer);
      window.removeEventListener('focus', check);
      window.removeEventListener('pointerdown', check, true);
      window.removeEventListener('keydown', check, true);
      window.removeEventListener('pagehide', leave);
    };
  }, [heldToken, lose]);

  const takeover = useCallback(async (reload) => {
    canEditRef.current = false;
    api.setEditToken(null);
    setState({ status: 'taking', message: '正在接管并加载最新工程…' });
    let token;
    try {
      const result = await api.acquireEditing(true);
      token = result.token;
      api.setEditToken(token, result.expires_in);
      setHeldToken(token);
      await reload();
      if (api.getEditToken() !== token) throw new Error('重新加载期间编辑权已失效，请再次接管。');
      canEditRef.current = true;
      setState({ status: 'editing', message: '当前页面持有工作区编辑权' });
    } catch (error) {
      lose(`接管失败：${api.errorDetail(error)}`);
      if (token) {
        try {
          await api.releaseEditing(token);
        } catch (releaseError) {
          console.warn('释放编辑权失败，将等待租约过期', releaseError);
        }
      }
      throw error;
    }
  }, [lose]);

  return { ...state, canEdit: state.status === 'editing', canEditRef, takeover };
}
