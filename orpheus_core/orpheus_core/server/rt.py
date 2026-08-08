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

_KIND_NAMES = {0: "RTC", 1: "TUNE", 2: "PROBE", 3: "STATE", 4: "CUSTOM"}
_FORM_NAMES = {0: "SCALAR", 1: "BULK", 2: "MODULE"}


def _msg_call_id(frame_hex: str) -> int:
    """从二进制消息帧（hex）解出 call_id（头 bits 的 bit25..10）。"""
    raw = bytes.fromhex(frame_hex)
    bits = int.from_bytes(raw[4:8], "little")
    return (bits >> 10) & 0xFFFF


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
        self._cmd_lines: list[str] = []  # RESOLVED / RVALUE / OK RW / ERR ... 响应行
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
                        if (
                            line.startswith("RESOLVED ")
                            or line.startswith("ERR RESOLVE ")
                            or line.startswith("RVALUE ")
                            or line.startswith("OK RW ")
                            or line.startswith("ERR RW ")
                            or line.startswith("OK RWB ")
                            or line.startswith("ERR RWB ")
                            or line.startswith("BULKVALUE ")
                            or line.startswith("ERR GETBULK ")
                            or line.startswith("MSGRSP ")
                            or line.startswith("MSGNONE")
                            or line.startswith("ERR MSG ")
                        ):
                            self._cmd_lines.append(line)
                        else:
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

    def _send_cmd_wait(self, line: str, timeout: float = 3.0) -> str:
        """发一条命令并等待一条命令响应行（RESOLVED/RVALUE/OK RW 等）。"""
        with self._lock:
            base = len(self._cmd_lines)
        self.send(line)
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                if len(self._cmd_lines) > base:
                    return self._cmd_lines[-1]
            time.sleep(0.02)
        raise RuntimeError(f"rt_host 命令超时: {line}")

    def resolve(self, data_id: int) -> dict[str, Any]:
        """RESOLVE <id>：内存透明查询（ID → 类型/长度/基址/偏移）。"""
        line = self._send_cmd_wait(f"RESOLVE {data_id}")
        if line.startswith("ERR RESOLVE"):
            raise RuntimeError(line)
        parts = line.split()
        d: dict[str, Any] = {
            "id": int(parts[1], 16),
            "kind": _KIND_NAMES.get(int(parts[2]), parts[2]),
            "form": _FORM_NAMES.get(int(parts[3]), parts[3]),
        }
        for tok in parts[4:]:
            if "=" in tok:
                key, value = tok.split("=", 1)
                d[key] = value
        return d

    def map_all(self) -> list[dict[str, Any]]:
        """MAP：dump 全表（数据点 + 模块包），等待输出稳定后返回。"""
        with self._lock:
            base = len(self._cmd_lines)
        self.send("MAP")
        deadline = time.time() + 3.0
        last_count = -1
        while time.time() < deadline:
            time.sleep(0.08)
            with self._lock:
                count = len(self._cmd_lines)
            if count > base and count == last_count:
                break
            last_count = count
        with self._lock:
            lines = list(self._cmd_lines[base:])
        return [
            self._parse_resolved_line(l) for l in lines if l.startswith("RESOLVED ")
        ]

    @staticmethod
    def _parse_resolved_line(line: str) -> dict[str, Any]:
        parts = line.split()
        d: dict[str, Any] = {
            "id": int(parts[1], 16),
            "kind": _KIND_NAMES.get(int(parts[2]), parts[2]),
            "form": _FORM_NAMES.get(int(parts[3]), parts[3]),
        }
        for tok in parts[4:]:
            if "=" in tok:
                key, value = tok.split("=", 1)
                d[key] = value
        return d

    def write_id(self, data_id: int, value: Any) -> None:
        """RW <id> <value>：按 ID 写（RTC/TUNE；PROBE/STATE 由注册表拒写）。"""
        line = self._send_cmd_wait(f"RW {data_id} {value}")
        if not line.startswith("OK RW"):
            raise RuntimeError(line)

    def read_id(self, data_id: int) -> Any:
        """RR <id>：按 ID 读回（RVALUE <id> <value>）。"""
        line = self._send_cmd_wait(f"RR {data_id}")
        if line.startswith("ERR RR"):
            raise RuntimeError(line)
        parts = line.split(maxsplit=2)
        raw = parts[2] if len(parts) == 3 else ""
        try:
            return float(raw)
        except ValueError:
            return raw

    def write_bulk_id(self, data_id: int, values: list[float]) -> None:
        """RWB <id> <n> <v0>...：按 ID 直写 BULK 槽。"""
        nums = " ".join(str(v) for v in values)
        line = self._send_cmd_wait(f"RWB {data_id} {len(values)} {nums}")
        if not line.startswith("OK RWB"):
            raise RuntimeError(line)

    def read_bulk(self, node: str | None = None, key: str | None = None,
                  data_id: int | None = None) -> list[float]:
        """GETBULK <node> <key> / RGB <id>：读回 BULK active bank（仅拷贝）。"""
        if data_id is not None:
            line = self._send_cmd_wait(f"RGB {data_id}")
        else:
            line = self._send_cmd_wait(f"GETBULK {node} {key}")
        if line.startswith("ERR GETBULK"):
            raise RuntimeError(line)
        parts = line.split()
        return [float(v) for v in parts[2:]]

    def msg(self, msg_hex: str, call_id: int, timeout: float = 3.0) -> str:
        """二进制消息：MSG <hex>；CALL → MSGRSP <hex>（按 call_id 匹配），NOTIFICATION/错误 → ''。"""
        with self._lock:
            base = len(self._cmd_lines)
        self.send(f"MSG {msg_hex}")
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                for line in self._cmd_lines[base:]:
                    if line.startswith("MSGRSP "):
                        rsp = line[7:].strip()
                        if _msg_call_id(rsp) == call_id:
                            return rsp
                    if line.startswith("MSGNONE") or line.startswith("ERR MSG "):
                        return ""
            time.sleep(0.02)
        raise RuntimeError("rt_host MSG 超时（call_id 未匹配）")

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
