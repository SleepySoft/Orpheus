"""RtSession 控制协议行格式测试（实时宿主 stdin 协议）。"""

from __future__ import annotations

import io

from orpheus_core.bridge import AsyncFileLogSink
from orpheus_core.server.rt import RtSession


class _FakeProc:
    """最小 Popen 替身：stdin 可写、stdout 立即 EOF、进程视为存活。"""

    def __init__(self) -> None:
        self.stdin = io.StringIO()
        self.stdout = io.StringIO("")

    def poll(self):
        return None


def test_write_bulk_line_format() -> None:
    """BULK <node> <key> <n> <v0> <v1> ...：整行构造正确，供 rt_host 解析。"""
    proc = _FakeProc()
    session = RtSession(proc)  # daemon reader 线程随 stdout EOF 立即退出
    session.write_bulk("fx__bq", "bq0.coefs", [1.0, -0.5, 0.25])
    assert proc.stdin.getvalue() == "BULK fx__bq bq0.coefs 3 1.0 -0.5 0.25\n"


def test_write_bulk_single_value() -> None:
    proc = _FakeProc()
    session = RtSession(proc)
    session.write_bulk("eq1", "taps", [64.0])
    assert proc.stdin.getvalue() == "BULK eq1 taps 1 64.0\n"


def test_runtime_logs_persist_without_probe_lines(tmp_path) -> None:
    proc = _FakeProc()
    proc.stdout = io.StringIO("LOG runtime ready\nPROBE mon rms 0.5\n")
    path = tmp_path / "runtime.log"
    session = RtSession(proc, log_sink=AsyncFileLogSink(path))
    session._reader.join(timeout=1.0)

    assert path.read_text(encoding="utf-8").splitlines() == ["LOG runtime ready"]
    assert session.snapshot()["probes"]["mon"]["rms"] == 0.5
