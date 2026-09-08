"""Bridge 日志 Sink；文件 IO 始终在后台线程执行。"""

from __future__ import annotations

import queue
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Protocol, runtime_checkable


@runtime_checkable
class LogSink(Protocol):
    def emit(self, line: str) -> bool: ...

    def close(self) -> None: ...

    def stats(self) -> dict[str, int]: ...


class NullLogSink:
    def emit(self, line: str) -> bool:  # noqa: ARG002
        return True

    def close(self) -> None:
        pass

    def stats(self) -> dict[str, int]:
        return {"written": 0, "dropped": 0, "queued": 0}


class AsyncFileLogSink:
    """有界、非阻塞提交的 UTF-8 文件 Sink。"""

    _STOP = object()

    def __init__(self, path: Path, *, capacity: int = 4096,
                 include_timestamp: bool = False):
        if capacity <= 0:
            raise ValueError("日志队列容量必须大于 0")
        self.path = Path(path)
        self.include_timestamp = include_timestamp
        self._queue: queue.Queue[str | object] = queue.Queue(maxsize=capacity)
        self._written = 0
        self._dropped = 0
        self._closed = False
        self._lock = threading.Lock()
        self._thread = threading.Thread(target=self._write_loop, daemon=True)
        self._thread.start()

    def emit(self, line: str) -> bool:
        with self._lock:
            if self._closed:
                return False
        try:
            self._queue.put_nowait(str(line))
            return True
        except queue.Full:
            with self._lock:
                self._dropped += 1
            return False

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._closed = True
        self._queue.put(self._STOP)
        self._thread.join()

    def stats(self) -> dict[str, int]:
        with self._lock:
            return {
                "written": self._written,
                "dropped": self._dropped,
                "queued": self._queue.qsize(),
            }

    def _write_loop(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.path.open("a", encoding="utf-8", newline="\n") as output:
            while True:
                item = self._queue.get()
                if item is self._STOP:
                    break
                if self.include_timestamp:
                    output.write(datetime.now(timezone.utc).isoformat())
                    output.write(" ")
                output.write(str(item))
                output.write("\n")
                output.flush()
                with self._lock:
                    self._written += 1
