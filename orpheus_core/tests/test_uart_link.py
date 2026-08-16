"""uart_link 组件测试：生成断言 + 生成工程构建 + stdio 链路端到端（无硬件全链路）。

端到端：orpheus_generated_app --link-stdio（stdin/stdout 二进制即链路），
Python 侧经 ProcessTransport 接 SerialSession，验证标量读写 / BULK / 探针上行 / msg 透传。
"""
from __future__ import annotations

import queue
import shutil
import struct
import subprocess
import threading
import time
from pathlib import Path

import pytest

from orpheus_core.builder import run_cmake_with_msvc_env
from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from orpheus_core.server.serial_session import SerialSession

ROOT = Path(__file__).resolve().parents[2]
MAIN_BUILD = ROOT / "build"


@pytest.fixture(scope="module")
def registry():
    reg = Registry()
    reg.add_search_path(ROOT / "components")
    reg.scan()
    return reg


def _project() -> Project:
    project = Project()
    project.graph = Graph(
        nodes={
            n.id: n
            for n in [
                Node(id="sig", component="orpheus.builtin.signal_gen",
                     params={"frequency": 440.0, "amplitude": 0.5, "channels": 1, "sample_rate": 48000}),
                Node(id="g", component="orpheus.builtin.gain", params={"gain_db": 0.0, "channels": 1}),
                Node(id="eq", component="orpheus.builtin.biquad_bank", params={"channels": 1}),
                Node(id="mon", component="orpheus.builtin.probe_rms", params={"channels": 1}),
                Node(id="sink", component="orpheus.builtin.null_sink", params={"channels": 1}),
                Node(id="link", component="orpheus.builtin.uart_link",
                     params={"link_name": "uart0", "probe_interval_ms": 100.0}),
            ]
        },
        connections=[
            Connection(PortRef.parse("sig:out"), PortRef.parse("g:in")),
            Connection(PortRef.parse("g:out"), PortRef.parse("eq:in")),
            Connection(PortRef.parse("eq:out"), PortRef.parse("mon:in")),
            Connection(PortRef.parse("mon:out"), PortRef.parse("sink:in")),
        ],
    )
    return project


@pytest.fixture(scope="module")
def plan(registry):
    return GraphCompiler(registry).compile(_project())


@pytest.fixture(scope="module")
def gen_dir(tmp_path_factory, plan, registry):
    out = tmp_path_factory.mktemp("uart_link_gen") / "gen"
    CodeGenerator(registry, ROOT).generate(plan, out)
    return out


# ------------------------------------------------------------------ 生成断言

def test_uart_link_excluded_from_plan(plan):
    assert "link" not in plan.nodes
    assert "link" not in plan.execution_order
    assert len(plan.declarations) == 1
    assert plan.declarations[0]["component"] == "orpheus.builtin.uart_link"


def test_generated_files(plan, gen_dir):
    assert (gen_dir / "src" / "olink.c").is_file()
    assert (gen_dir / "include" / "orpheus_olink.h").is_file()
    hdr = (gen_dir / "include" / "orpheus_link_uart0.h").read_text(encoding="utf-8")
    core = (gen_dir / "src" / "orpheus_link_uart0.c").read_text(encoding="utf-8")
    hooks = (gen_dir / "src" / "orpheus_link_hooks_uart0.c").read_text(encoding="utf-8")
    cmake = (gen_dir / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "orpheus_link_uart0_init" in hdr
    assert "orpheus_link_uart0_send" in hdr
    assert "orpheus_link_uart0_feed" in hdr
    assert "orpheus_link_uart0_poll" in hdr
    assert "ORPHEUS_LINK_UART0_PROBE_INTERVAL_MS 100.0" in hdr
    # 探针表含工程内 PROBE 数据点
    probe_ids = [e for e in plan.id_map if e["kind"] == "PROBE"]
    assert probe_ids and all(f"0x{e['id']:08X}U" in core for e in probe_ids)
    assert "orpheus_control_message" in core
    assert "USER CODE BEGIN send" in hooks and "USER CODE END init" in hooks
    assert "src/olink.c" in cmake
    assert "src/orpheus_link_uart0.c" in cmake
    assert "src/orpheus_link_hooks_uart0.c" in cmake
    assert "ORPHEUS_LINK_STDIO" in cmake
    # main.c：init 调用 + --link-stdio 模式
    main_c = (gen_dir / "src" / "main.c").read_text(encoding="utf-8")
    assert "orpheus_link_uart0_init();" in main_c
    assert "--link-stdio" in main_c


# ------------------------------------------------------------------ 构建 + 端到端

class ProcessTransport:
    """把子进程 stdin/stdout 包装成 SerialSession 的字节传输（泵线程短读）。"""

    def __init__(self, argv: list[str], cwd: Path):
        self.p = subprocess.Popen(argv, cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.q: queue.Queue[bytes] = queue.Queue()
        self._t = threading.Thread(target=self._pump, daemon=True)
        self._t.start()

    def _pump(self) -> None:
        # read1：有数据即返回（不等满缓冲），避免按字节搬运拖垮吞吐
        read1 = getattr(self.p.stdout, "read1", None) or self.p.stdout.read
        while True:
            chunk = read1(4096)
            if not chunk:
                break
            self.q.put(chunk)

    def write(self, data: bytes) -> int:
        self.p.stdin.write(data)
        self.p.stdin.flush()
        return len(data)

    def read(self, n: int) -> bytes:  # noqa: ARG002
        try:
            return self.q.get(timeout=0.05)
        except queue.Empty:
            return b""

    def close(self) -> None:
        try:
            self.p.kill()
            self.p.wait(timeout=3)
        except Exception:
            pass


def _build_generated(gen: Path) -> Path:
    bdir = gen / "build"
    exe = bdir / ("orpheus_generated_app.exe" if __import__("sys").platform == "win32"
                  else "orpheus_generated_app")
    if exe.exists():
        return exe
    r = run_cmake_with_msvc_env(["cmake", "-S", str(gen), "-B", str(bdir), "-G", "Ninja"],
                                gen, MAIN_BUILD)
    assert r.returncode == 0, (r.stdout or "")[-2000:] + (r.stderr or "")[-2000:]
    r = run_cmake_with_msvc_env(["cmake", "--build", str(bdir)], bdir, MAIN_BUILD)
    assert r.returncode == 0, (r.stdout or "")[-2000:] + (r.stderr or "")[-2000:]
    assert exe.exists()
    return exe


@pytest.fixture(scope="module")
def link_session(gen_dir, plan):
    if shutil.which("cmake") is None:
        pytest.skip("cmake 不可用")
    exe = _build_generated(gen_dir)
    transport = ProcessTransport([str(exe), "--link-stdio"], cwd=gen_dir)
    session = SerialSession(transport, plan.id_map, call_timeout=0.5, call_retries=3)
    yield session, plan
    session.close()
    transport.close()


def test_e2e_scalar_write_read(link_session):
    s, plan = link_session
    e = next(e for e in plan.id_map if e["key"] == "gain_db")
    s.write_id(e["id"], -6.0)
    assert s.read_id(e["id"]) == pytest.approx(-6.0, abs=1e-3)


def test_e2e_bulk_write_read(link_session):
    s, plan = link_session
    e = next(e for e in plan.id_map if e["key"] == "bq0.coefs")
    vals = [0.9, 0.1, -0.2, -1.0, 0.5]
    s.write_bulk_id(e["id"], vals)
    got = s.read_bulk(data_id=e["id"])
    assert got == pytest.approx(vals, abs=1e-5)


def test_e2e_msg_passthrough(link_session):
    from orpheus_core.link import message
    s, plan = link_session
    e = next(e for e in plan.id_map if e["key"] == "gain_db")
    frame = message.make_call(e["id"], 0x1234, struct.pack("<f", -1.0))
    rsp_hex = s.msg(frame.hex(), 0x1234)
    rsp = message.parse_frame(bytes.fromhex(rsp_hex))
    assert rsp["type"] == message.RESPONSE and rsp["call_id"] == 0x1234 and not rsp["error"]
    assert s.read_id(e["id"]) == pytest.approx(-1.0, abs=1e-3)


def test_e2e_probe_notifications(link_session):
    """探针泵上行：probe_rms 的 NOTIFICATION 应进 snapshot probes（幅度≈0.5 正弦的 RMS）。"""
    s, plan = link_session
    e = next(e for e in plan.id_map if e["key"] == "rms")
    # 模块级会话：前序测试改过 gain 与 bq0 系数，先归位（0 dB + 直通 biquad）再断言基准 RMS
    gain = next(g for g in plan.id_map if g["key"] == "gain_db")
    s.write_id(gain["id"], 0.0)
    coefs = next(c for c in plan.id_map if c["key"] == "bq0.coefs")
    s.write_bulk_id(coefs["id"], [1.0, 0.0, 0.0, 0.0, 0.0])
    deadline = time.time() + 5.0
    value = None
    while time.time() < deadline:
        v = s.snapshot()["probes"].get(e["node"], {}).get("rms")
        if v is not None and abs(v - 0.3536) < 0.02:
            value = v
            break
        time.sleep(0.05)
    assert value is not None, f"未收到符合预期的探针值（当前 {v}）"
