"""Realtime host session: manage an rt_host subprocess with stdin/stdout control.

Protocol (see rt_host.cpp):
- stdin:  `SET <node> <param> <value>` / `GET <node> <param>` / `STOP`
- stdout: `LOG ...` lifecycle lines, `PROBE <node> <param> <value>` every 200ms,
          `OK/ERR ...` command echoes, component printf output (non-RT code only).
"""

from __future__ import annotations

import json
import subprocess
import threading
import time
from collections import deque
from pathlib import Path
from typing import Any

MAX_LOG_LINES = 500


def parse_probe_line(line: str) -> tuple[str, str, Any] | None:
    """Parse one host stdout line into (node, param, value) or None.

    Supports `PROBE <node> <param> <value>` (scalar) and
    `PROBE_JSON <node> <param> <json>` (structured value).
    """
    parts = line.split(maxsplit=3)
    if len(parts) != 4:
        return None
    kind, node, param, raw = parts
    if kind == "PROBE":
        try:
            value: Any = float(raw)
        except ValueError:
            value = raw
        return node, param, value
    if kind == "PROBE_JSON":
        try:
            value = json.loads(raw)
        except (json.JSONDecodeError, ValueError):
            value = raw
        return node, param, value
    return None


class RtSession:
    def __init__(self, proc: subprocess.Popen):
        self.proc = proc
        self.started_at = time.time()
        self._logs: deque[str] = deque(maxlen=MAX_LOG_LINES)
        self._probes: dict[str, dict[str, Any]] = {}
        self._lock = threading.Lock()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def _read_loop(self) -> None:
        try:
            # readline() (not iteration): file iteration uses readahead and can
            # hold back lines on pipes
            while True:
                line = self.proc.stdout.readline()
                if not line:
                    break
                line = line.rstrip("\r\n")
                parsed = parse_probe_line(line)
                if parsed is not None:
                    node, param, value = parsed
                    with self._lock:
                        self._probes.setdefault(node, {})[param] = value
                else:
                    with self._lock:
                        self._logs.append(line)
        except (ValueError, OSError):
            pass  # stream closed

    @property
    def running(self) -> bool:
        return self.proc.poll() is None

    def send(self, line: str) -> None:
        if not self.running:
            raise RuntimeError("rt host process is not running")
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def set_parameter(self, node: str, param: str, value: Any) -> None:
        self.send(f"SET {node} {param} {value}")

    def write_bulk(self, node: str, key: str, values: list[float]) -> None:
        """BULK <node> <key> <n> <v0> <v1> ...：直写组件注册的 BULK 槽。"""
        nums = " ".join(str(v) for v in values)
        self.send(f"BULK {node} {key} {len(values)} {nums}")

    def stop(self, timeout: float = 3.0) -> None:
        if not self.running:
            return
        try:
            self.send("STOP")
            self.proc.wait(timeout=timeout)
        except Exception:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2.0)
            except Exception:
                self.proc.kill()

    def snapshot(self, max_logs: int = 200) -> dict[str, Any]:
        with self._lock:
            logs = list(self._logs)[-max_logs:]
            probes = {n: dict(p) for n, p in self._probes.items()}
        return {
            "running": self.running,
            "exit_code": self.proc.poll(),
            "started_at": self.started_at,
            "logs": logs,
            "probes": probes,
        }


class RtSessionManager:
    """One realtime session per project."""

    def __init__(self) -> None:
        self._sessions: dict[str, RtSession] = {}
        self._lock = threading.Lock()

    def start(self, name: str, argv: list[str], cwd: Path) -> RtSession:
        with self._lock:
            old = self._sessions.get(name)
            if old and old.running:
                raise RuntimeError(f"realtime session already running for {name}")
            proc = subprocess.Popen(
                argv,
                cwd=cwd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
            session = RtSession(proc)
            self._sessions[name] = session
            return session

    def get(self, name: str) -> RtSession | None:
        with self._lock:
            return self._sessions.get(name)

    def stop(self, name: str) -> None:
        with self._lock:
            session = self._sessions.get(name)
        if session:
            session.stop()
