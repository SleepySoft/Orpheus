"""后端拥有的 Bridge 子进程会话：握手、日志与升级停止。"""

from __future__ import annotations

import subprocess
import threading
import time
from collections import deque
from pathlib import Path
from typing import Any, Sequence

from orpheus_core.bridge import (
    AsyncFileLogSink,
    BridgeSession,
    LengthPrefixCodec,
    ProcessTransport,
)


class ProcessBridgeSession(BridgeSession):
    def __init__(
        self,
        argv: Sequence[str],
        cwd: Path,
        id_map: list[dict[str, Any]],
        *,
        expected_id_map_hash: int,
        log_path: Path,
        connect_timeout: float = 10.0,
        probe_interval: float = 0.2,
        auto_stop_after: float | None = None,
    ):
        self._endpoint_logs: deque[str] = deque(maxlen=500)
        self._endpoint_log_sink = AsyncFileLogSink(log_path, include_timestamp=True)
        transport = ProcessTransport(argv, cwd=cwd, stderr=subprocess.PIPE)
        self.proc = transport.process
        super().__init__(
            transport,
            LengthPrefixCodec(),
            id_map,
            call_timeout=0.5,
            call_retries=max(1, int(connect_timeout / 0.5)),
            probe_interval=probe_interval,
        )
        self._stderr_thread = threading.Thread(target=self._read_stderr, daemon=True)
        self._stderr_thread.start()
        try:
            self.connect(expected_id_map_hash=expected_id_map_hash)
        except Exception:
            self.close()
            raise
        self._auto_stop_timer: threading.Timer | None = None
        if auto_stop_after is not None and auto_stop_after > 0:
            self._auto_stop_timer = threading.Timer(auto_stop_after, self.stop)
            self._auto_stop_timer.daemon = True
            self._auto_stop_timer.start()

    def _read_stderr(self) -> None:
        stream = self.proc.stderr
        if stream is None:
            return
        while True:
            raw = stream.readline()
            if not raw:
                break
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            self._endpoint_logs.append(line)
            self._endpoint_log_sink.emit(line)

    def stop(self, timeout: float = 3.0) -> None:
        timer = getattr(self, "_auto_stop_timer", None)
        if timer is not None and timer is not threading.current_thread():
            timer.cancel()
        if self.proc.poll() is None and self.hello is not None:
            try:
                self.stop_endpoint()
                self.proc.wait(timeout=timeout)
            except Exception:
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=2.0)
        self.close()

    def close(self) -> None:
        if getattr(self, "_closed", None) is not None and self._closed.is_set():
            return
        super().close()
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=2.0)
        if hasattr(self, "_stderr_thread"):
            self._stderr_thread.join(timeout=1.0)
        if hasattr(self, "_endpoint_log_sink"):
            self._endpoint_log_sink.close()

    def snapshot(self, max_logs: int = 200) -> dict[str, Any]:
        result = super().snapshot(max_logs=max_logs)
        result["running"] = self.proc.poll() is None and result["running"]
        result["exit_code"] = self.proc.poll()
        result["logs"] = list(self._endpoint_logs)[-max_logs:] + result["logs"]
        result["pid"] = self.proc.pid
        result["endpoint_ready"] = self.hello is not None
        result["bridge_ready"] = self.identity is not None and self.identity_verified
        return result
