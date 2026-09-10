"""工程运行会话的互斥、停止与回收。"""

from __future__ import annotations

import threading
from typing import Any


class RuntimeSessionManager:
    """每个工程最多持有一个 Bridge 会话。"""

    def __init__(self) -> None:
        self._sessions: dict[str, Any] = {}
        self._lock = threading.Lock()

    def adopt(self, name: str, session: Any) -> None:
        with self._lock:
            old = self._sessions.get(name)
            if old and old.running:
                raise RuntimeError(f"realtime session already running for {name}")
            if old:
                old.close()
            self._sessions[name] = session

    def get(self, name: str) -> Any | None:
        with self._lock:
            return self._sessions.get(name)

    def stop(self, name: str) -> None:
        with self._lock:
            session = self._sessions.get(name)
        if session:
            session.stop()

    def stop_all(self) -> None:
        with self._lock:
            sessions = list(self._sessions.values())
            self._sessions.clear()
        for session in sessions:
            try:
                session.stop()
            except Exception:
                try:
                    session.close()
                except Exception:
                    pass