"""二进制消息协议（CALL / RESPONSE / NOTIFICATION + call_id）测试。"""

from __future__ import annotations

import json
import struct
import subprocess
import uuid
from dataclasses import asdict
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from orpheus_core.builder import run_cmake_with_msvc_env
from orpheus_core.compiler import GraphCompiler
from orpheus_core.generator import CodeGenerator
from orpheus_core.link import message
from orpheus_core.project import DriveConnection, Node, PortRef, Project
from orpheus_core.registry import Registry
from orpheus_core.server.app import create_app

ROOT = Path(__file__).resolve().parents[2]
_CREATED: list[str] = []

MSG_CALL, MSG_RESPONSE, MSG_NOTIFICATION = 0, 1, 2
FLAG_ERROR = 0x8  # 解析出的 4 位 flags 窗口内：bit29（错误）→ bit3


def make_msg(route: int, call_id: int, msg_type: int = MSG_CALL,
             payload: bytes = b"", flags: int = 0) -> bytes:
    words = (len(payload) + 3) // 4
    bits = (msg_type << 30) | (flags << 26) | (call_id << 10) | words
    return struct.pack("<II", route, bits) + payload.ljust(words * 4, b"\x00")


def parse_msg(frame: bytes) -> dict:
    route, bits = struct.unpack("<II", frame[:8])
    return {
        "route": route,
        "type": (bits >> 30) & 0x3,
        "flags": (bits >> 26) & 0xF,
        "call_id": (bits >> 10) & 0xFFFF,
        "payload": frame[8: 8 + (bits & 0x3FF) * 4],
    }


def f32(v: float) -> bytes:
    return struct.pack("<f", v)


def _new_name() -> str:
    name = f"msg_{uuid.uuid4().hex[:8]}"
    _CREATED.append(name)
    return name


@pytest.fixture(autouse=True)
def _cleanup_projects():
    yield
    if _CREATED:
        with TestClient(create_app(ROOT)) as client:
            for name in _CREATED:
                try:
                    client.delete(f"/api/projects/{name}")
                except Exception:
                    pass
    _CREATED.clear()


def _plan_of(client, name: str, double_bank: str = "auto") -> tuple[Path, dict]:
    resp = client.post(
        "/api/projects", json={"name": name, "from_example": "dsp_model_reference"}
    )
    assert resp.status_code == 201, resp.text
    if double_bank != "auto":
        doc = client.get(f"/api/projects/{name}").json()
        doc["double_bank"] = double_bank
        assert client.put(f"/api/projects/{name}", json=doc).status_code == 200
    assert client.post(f"/api/projects/{name}/compile").status_code == 200
    assert client.post(f"/api/projects/{name}/generate").status_code == 200
    cwd = ROOT / "workspace" / name
    plan = json.loads((cwd / "project.plan.json").read_text(encoding="utf-8"))
    return cwd, plan


@pytest.mark.parametrize("size", [0, 1, 4091, 4092])
def test_python_message_payload_boundary(size):
    payload = b"x" * size
    raw = message.make_call(123, 0xA55A, payload)
    parsed = message.parse_frame(raw)
    assert parsed["call_id"] == 0xA55A
    assert parsed["payload"] == payload.ljust((size + 3) // 4 * 4, b"\x00")
    assert len(raw) <= 4100


@pytest.mark.parametrize("size", [4093, 4096, 8192])
def test_python_message_rejects_oversized_payload(size):
    with pytest.raises(ValueError, match="1023"):
        message.make_call(123, 1, b"x" * size)


@pytest.mark.parametrize("raw", [
    b"",
    message.make_call(123, 1, b"abcd")[:-1],
    message.make_call(123, 1) + b"abcd",
])
def test_python_message_rejects_mismatched_length(raw):
    with pytest.raises(ValueError):
        message.parse_frame(raw)


@pytest.fixture(scope="module")
def boundary_backends(tmp_path_factory):
    from test_generated_double_bank import _build

    directory = tmp_path_factory.mktemp("message_boundaries")
    registry = Registry([ROOT / "components"])
    registry.scan()
    project = Project(flow_mode="task_sequence", block_size=32)
    project.graph.nodes = {
        "clock": Node("clock", "orpheus.builtin.task_source", params={"frequency_hz": 100}),
        "gain": Node("gain", "orpheus.builtin.gain", params={"channels": 1}),
        "relay": Node("relay", "orpheus.builtin.bulk_relay",
                      params={"count": 2048, "initial_values": ",".join(["0"] * 2048)}),
    }
    project.drive_connections = [
        DriveConnection(PortRef("clock", "out"), PortRef("gain", "in")),
        DriveConnection(PortRef("gain", "out"), PortRef("relay", "in")),
    ]
    plan = GraphCompiler(registry).compile(project)
    path = directory / "plan.json"
    path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = directory / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    (generated / "src/message_bounds_test.c").write_bytes(
        (ROOT / "tests/message_bounds_runner.cpp").read_bytes()
    )
    with (generated / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
        stream.write(
            "\nadd_executable(message_bounds_test src/message_bounds_test.c)\n"
            "target_compile_definitions(message_bounds_test PRIVATE ORPHEUS_TEST_GENERATED)\n"
            "target_link_libraries(message_bounds_test PRIVATE orpheus_graph)\n"
        )
    executable = _build(generated, directory)
    commands = {
        "dynamic": [str(ROOT / "build" / "orpheus_runtime.exe"),
                    str(path), str(ROOT / "build" / "components")],
        "generated": [str(executable), "0"],
    }
    bulk_id = next(entry["id"] for entry in plan.id_map
                   if entry["node"] == "relay" and entry["key"] == "values_storage")
    return directory, commands, bulk_id


def _boundary_run(backends, backend, *args):
    directory, commands, _ = backends
    return subprocess.run(
        [*commands[backend], *args], cwd=directory, capture_output=True,
        text=True, encoding="utf-8",
    )


@pytest.mark.parametrize("backend", ["dynamic", "generated"])
def test_bulk_response_cannot_overflow_header(boundary_backends, backend):
    _, _, bulk_id = boundary_backends
    result = _boundary_run(boundary_backends, backend, "--msg",
                           message.make_call(bulk_id, 0xA55A).hex())
    assert result.returncode == 0, result.stderr
    raw = bytes.fromhex(result.stdout.strip().removeprefix("MSGRSP "))
    response = message.parse_frame(raw)
    assert len(raw) == 8
    assert response["route"] == bulk_id and response["call_id"] == 0xA55A
    assert response["error"] and response["payload"] == b""


@pytest.mark.parametrize("backend", ["dynamic", "generated"])
@pytest.mark.parametrize("size", [0, 4092])
def test_hook_response_at_payload_limit(boundary_backends, backend, size):
    custom_id = 0x40000001
    payload = b"x" * size
    result = _boundary_run(
        boundary_backends, backend, "--echo-hook", str(custom_id),
        "--msg", message.make_call(custom_id, 0xA55A, payload).hex(),
    )
    assert result.returncode == 0, result.stderr
    raw = bytes.fromhex(result.stdout.strip().removeprefix("MSGRSP "))
    response = message.parse_frame(raw)
    assert len(raw) == 8 + size
    assert not response["error"] and response["payload"] == payload
    assert response["route"] == custom_id and response["call_id"] == 0xA55A


@pytest.mark.parametrize("backend", ["dynamic", "generated"])
@pytest.mark.parametrize("request_frame", [
    message.make_call(0x40000001, 1, b"abcd")[:-1],
    message.make_call(0x40000001, 1) + b"abcd",
    make_msg(0x40000001, 1, payload=b"x" * 4096),
])
def test_native_message_rejects_mismatched_length(boundary_backends, backend, request_frame):
    result = _boundary_run(boundary_backends, backend, "--msg", request_frame.hex())
    assert "ERR MSG" in result.stdout, result.stdout + result.stderr


@pytest.mark.parametrize("backend", ["dynamic", "generated"])
def test_native_message_output_and_hook_bounds(boundary_backends, backend):
    directory, commands, bulk_id = boundary_backends
    plan = json.loads((directory / "plan.json").read_text(encoding="utf-8"))
    scalar_id = str(next(entry["id"] for entry in plan["id_map"]
                         if entry["node"] == "gain" and entry["key"] == "gain_db"))
    command = (
        [str(ROOT / "build/tests/message_bounds_runner.exe"), *commands["dynamic"][1:], scalar_id, str(bulk_id)]
        if backend == "dynamic" else
        [str(directory / "generated/build/message_bounds_test.exe"), scalar_id, str(bulk_id)]
    )
    result = subprocess.run(command, cwd=directory, capture_output=True,
                            text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.skipif(
    not (ROOT / "build" / "orpheus_runtime.exe").exists()
    or not (ROOT / "build" / "components").exists(),
    reason="runtime and components not built",
)
def test_runtime_message_protocol() -> None:
    name = _new_name()
    with TestClient(create_app(ROOT)) as client:
        cwd, plan = _plan_of(client, name)
    by_key = {(e["node"], e["key"]): e for e in plan["id_map"]}
    gain_id = by_key[("front__trim", "gain_db")]["id"]
    rms_id = by_key[("front__mon", "rms")]["id"]
    bulk_id = by_key[("front__eq_bank__bq", "bq0.coefs")]["id"]
    custom_id = (0x4 << 28) | (0 << 16) | 1  # CUSTOM kind（用户自定义）
    exe = ROOT / "build" / "orpheus_runtime.exe"
    comps = ROOT / "build" / "components"

    def run(*args: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [str(exe), str(cwd / "project.plan.json"), str(comps), *args],
            capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
        )

    # CALL 写 RTC → 同步 RESPONSE（call_id 回显、无错误、无 payload）
    r = run("--msg", make_msg(gain_id, 0x1234, payload=f32(-6.0)).hex())
    assert r.returncode == 0 and r.stdout.startswith("MSGRSP "), r.stderr
    resp = parse_msg(bytes.fromhex(r.stdout.strip()[7:]))
    assert resp["type"] == MSG_RESPONSE and resp["call_id"] == 0x1234
    assert resp["route"] == gain_id and resp["flags"] == 0 and resp["payload"] == b""

    # 同进程写后读 → payload = -6.0
    r = run("--msg", make_msg(gain_id, 0x11, payload=f32(-6.0)).hex(),
            "--msg", make_msg(gain_id, 0x22).hex())
    lines = [l for l in r.stdout.splitlines() if l.startswith("MSGRSP ")]
    assert len(lines) == 2
    assert abs(struct.unpack("<f", parse_msg(bytes.fromhex(lines[1][7:]))["payload"])[0] + 6.0) < 1e-3

    # PROBE 读有值；PROBE 写被拒（错误位）
    r = run("--msg", make_msg(rms_id, 0x33).hex())
    resp = parse_msg(bytes.fromhex(r.stdout.strip()[7:]))
    assert resp["flags"] == 0 and len(resp["payload"]) == 4
    r = run("--msg", make_msg(rms_id, 0x44, payload=f32(1.0)).hex())
    assert parse_msg(bytes.fromhex(r.stdout.strip()[7:]))["flags"] & FLAG_ERROR

    # CUSTOM：无 hook → 错误；echo hook → 原样返回；NOTIFICATION → MSGNONE
    r = run("--msg", make_msg(custom_id, 0x55, payload=b"HI!").hex())
    assert parse_msg(bytes.fromhex(r.stdout.strip()[7:]))["flags"] & FLAG_ERROR
    r = run("--echo-hook", str(custom_id),
            "--msg", make_msg(custom_id, 0x66, payload=b"HELLO").hex())
    resp = parse_msg(bytes.fromhex(r.stdout.strip()[7:]))
    assert resp["flags"] == 0 and resp["payload"].rstrip(b"\x00") == b"HELLO"
    r = run("--echo-hook", str(custom_id),
            "--msg", make_msg(custom_id, 0x77, MSG_NOTIFICATION, b"EVENT").hex())
    assert r.stdout.strip() == "MSGNONE"

    # BULK 双 bank：写影子未提交读旧值；--run 1 提交后读新值
    vals = struct.pack("<5f", 1, 2, 3, 4, 5)
    r = run("--msg", make_msg(bulk_id, 0x88, payload=vals).hex(),
            "--msg", make_msg(bulk_id, 0x99).hex())
    lines = [l for l in r.stdout.splitlines() if l.startswith("MSGRSP ")]
    assert struct.unpack("<5f", parse_msg(bytes.fromhex(lines[1][7:]))["payload"])[0] != 1.0
    r = run("--msg", make_msg(bulk_id, 0xAA, payload=vals).hex(),
            "--run", "1",
            "--msg", make_msg(bulk_id, 0xBB).hex())
    lines = [l for l in r.stdout.splitlines() if l.startswith("MSGRSP ")]
    assert struct.unpack("<5f", parse_msg(bytes.fromhex(lines[1][7:]))["payload"]) == (1, 2, 3, 4, 5)


def test_generated_message_protocol() -> None:
    name = _new_name()
    with TestClient(create_app(ROOT)) as client:
        cwd, plan = _plan_of(client, name)
    gen = cwd / "generated"
    build_dir = gen / "build"
    args = ["cmake", "-S", str(gen), "-B", str(build_dir), "-G", "Ninja"]
    cache = ROOT / "build" / "CMakeCache.txt"
    for line in cache.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("CMAKE_C_COMPILER:") and "=" in line:
            args.append(f"-DCMAKE_C_COMPILER={line.split('=', 1)[1]}")
    assert run_cmake_with_msvc_env(args, cwd=cwd, build_dir=ROOT / "build").returncode == 0
    assert run_cmake_with_msvc_env(
        ["cmake", "--build", str(build_dir)], cwd=cwd, build_dir=ROOT / "build"
    ).returncode == 0
    exe = build_dir / "orpheus_generated_cli.exe"

    by_key = {(e["node"], e["key"]): e for e in plan["id_map"]}
    gain_id = by_key[("front__trim", "gain_db")]["id"]
    custom_id = (0x4 << 28) | (0 << 16) | 1

    def run(*args: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [str(exe), "0", *args],
            capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd,
        )

    # 标量 CALL 写后读（生成侧直写）
    r = run("--msg", make_msg(gain_id, 0x21, payload=f32(-3.0)).hex(),
            "--msg", make_msg(gain_id, 0x22).hex())
    lines = [l for l in r.stdout.splitlines() if l.startswith("MSGRSP ")]
    assert len(lines) == 2
    assert abs(struct.unpack("<f", parse_msg(bytes.fromhex(lines[1][7:]))["payload"])[0] + 3.0) < 1e-3

    # CUSTOM：无 hook 错误；echo hook 原样返回；NOTIFICATION → MSGNONE
    r = run("--msg", make_msg(custom_id, 0x31, payload=b"HI").hex())
    assert parse_msg(bytes.fromhex(r.stdout.strip()[7:]))["flags"] & FLAG_ERROR
    r = run("--echo-hook", str(custom_id),
            "--msg", make_msg(custom_id, 0x32, payload=b"HELLO").hex())
    resp = parse_msg(bytes.fromhex(r.stdout.strip()[7:]))
    assert resp["flags"] == 0 and resp["payload"].rstrip(b"\x00") == b"HELLO"
    r = run("--echo-hook", str(custom_id),
            "--msg", make_msg(custom_id, 0x33, MSG_NOTIFICATION, b"EVENT").hex())
    assert r.stdout.strip() == "MSGNONE"
