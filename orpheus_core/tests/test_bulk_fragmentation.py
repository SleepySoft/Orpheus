"""同一协议下的大 BULK 事务：双路径、交错、原子提交及稳定快照。"""

from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, replace
import io
import json
from pathlib import Path
import shutil
import subprocess
import threading
import time

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.bridge import BridgeSession, LengthPrefixCodec, StreamTransport
from orpheus_core.generator import CodeGenerator
from orpheus_core.link import message
from orpheus_core.project import DriveConnection, Node, PortRef, Project
from orpheus_core.registry import Registry
from orpheus_core.server.bridge_process_session import ProcessBridgeSession

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).parent / "fixtures" / "bridge_bulk"


@pytest.mark.parametrize("timeout", [0.0, -1.0, float("inf"), float("nan")])
def test_bulk_timeout_must_be_finite_positive(timeout):
    transport = StreamTransport(io.BytesIO(), io.BytesIO())
    try:
        with pytest.raises(ValueError, match="有限正数"):
            BridgeSession(transport, LengthPrefixCodec(), [], bulk_timeout=timeout)
    finally:
        transport.close()


@pytest.fixture(scope="module", params=["off", "auto"])
def fragment_backends(tmp_path_factory, request):
    from test_generated_double_bank import _build

    directory = tmp_path_factory.mktemp(f"bulk_fragments_{request.param}")
    components = directory / "components"
    components.mkdir()
    compiler = next(
        line.split("=", 1)[1]
        for line in (ROOT / "build" / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
        if line.startswith("CMAKE_C_COMPILER:")
    )
    result = subprocess.run(
        [compiler, "-std=c11", "-shared", "-DORPHEUS_BUILDING_COMPONENT",
         "-I", str(ROOT / "orpheus_abi" / "include"), "-I", str(FIXTURE / "include"),
         str(FIXTURE / "src" / "bridge_bulk_test.c"),
         "-o", str(components / "liborpheus_test_bridge_bulk.dll")],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert result.returncode == 0, result.stdout + result.stderr
    shutil.copy2(ROOT / "build" / "components" / "liborpheus_builtin_task_source.dll", components)
    registry = Registry([ROOT / "components", FIXTURE])
    registry.scan()
    project = Project(flow_mode="task_sequence", block_size=32, double_bank=request.param)
    project.graph.nodes = {
        "clock": Node("clock", "orpheus.builtin.task_source", params={"frequency_hz": 100}),
        "data": Node("data", "orpheus.test.bridge_bulk"),
    }
    project.drive_connections = [
        DriveConnection(PortRef("clock", "out"), PortRef("data", "in")),
    ]
    plan = GraphCompiler(registry).compile(project)
    plan.duration_frames = plan.sample_rate * 60
    path = directory / "plan.json"
    path.write_text(json.dumps(asdict(plan)), encoding="utf-8")
    generated = directory / "generated"
    CodeGenerator(registry, ROOT).generate(plan, generated)
    executable = _build(generated, directory)
    return directory, plan, {
        "dynamic": [str(ROOT / "build" / "orpheus_runtime.exe"),
                    str(path), str(components), "--bridge-stdio", "--pace"],
        "generated": [str(executable), "0", "--bridge-stdio"],
    }


@pytest.fixture(params=["dynamic", "generated"])
def session(fragment_backends, request, tmp_path):
    directory, plan, commands = fragment_backends
    instance = ProcessBridgeSession(
        commands[request.param], directory, plan.id_map,
        expected_id_map_hash=plan.bridge_identity["id_map_hash"],
        log_path=tmp_path / "bridge.log", probe_interval=0,
    )
    instance.bulk_timeout = 2.0
    try:
        assert instance.capabilities.bulk_fragment
        yield instance
    finally:
        instance.stop()


def data_id(session, key):
    return session._entry("data", key)["id"]


def send(session, key, operation, transfer, offset, count, total, values=None):
    entry = session._entry("data", key)
    payload = message.encode_bulk(entry["type"], values) if values is not None else b""
    return session._bulk_fragment(
        entry["id"], operation, transfer, offset, count, total, payload,
    )


def test_automatic_float_and_int_roundtrip(session):
    integers = [(-1 if i % 2 else 1) * (2**30 + i) for i in range(2048)]
    floats = [(i - 1024) * 0.125 for i in range(2048)]
    session.write_bulk_id(data_id(session, "integers"), integers)
    session.write_bulk("data", "floats", floats)
    assert session.read_bulk("data", "integers") == integers
    assert session.read_bulk(data_id=data_id(session, "floats")) == floats
    assert session.read_bulk("data", "small") == [0] * 17


def test_negotiated_smaller_frame_and_missing_capability(session):
    negotiated = session.capabilities
    session.capabilities = replace(negotiated, max_frame=256)
    values = [2147483647, -2147483648] * 1024
    session.write_bulk("data", "integers", values)
    assert session.read_bulk("data", "integers") == values
    session.capabilities = replace(negotiated, bulk_fragment=False)
    with pytest.raises(RuntimeError, match="分片能力"):
        session.write_bulk("data", "integers", values)
    with pytest.raises(RuntimeError, match="分片能力"):
        session.read_bulk("data", "integers")
    assert session.read_bulk("data", "small") == [0] * 17
    session.capabilities = negotiated
    assert session.read_bulk("data", "integers") == values


def test_interleaved_out_of_order_writes_and_retry(session):
    a = [i - 1024 for i in range(2048)]
    b = [i * 0.25 for i in range(2048)]
    first, second = session._next_transfer_id(), session._next_transfer_id()
    send(session, "integers", message.BULK_WRITE, first, 1018, 1018, 2048, a[1018:2036])
    send(session, "floats", message.BULK_WRITE, second, 0, 1018, 2048, b[:1018])
    assert session.read_bulk("data", "integers") == [0] * 2048
    assert session.read_bulk("data", "floats") == [0.0] * 2048
    with pytest.raises(RuntimeError, match="占用"):
        send(session, "integers", message.BULK_WRITE, session._next_transfer_id(), 0, 1, 2048, [7])
    with pytest.raises(RuntimeError, match="尚未收齐"):
        send(session, "integers", message.BULK_COMMIT, first, 0, 0, 2048)
    send(session, "integers", message.BULK_WRITE, first, 1018, 1018, 2048, a[1018:2036])
    with pytest.raises(RuntimeError, match="冲突"):
        send(session, "integers", message.BULK_WRITE, first, 1018, 1, 2048, [12345])
    with pytest.raises(RuntimeError, match="冲突"):
        send(session, "floats", message.BULK_WRITE, first, 0, 1, 2048, [0.0])
    send(session, "integers", message.BULK_WRITE, first, 2036, 12, 2048, a[2036:])
    send(session, "floats", message.BULK_WRITE, second, 1018, 1018, 2048, b[1018:2036])
    send(session, "integers", message.BULK_WRITE, first, 0, 1018, 2048, a[:1018])
    send(session, "floats", message.BULK_WRITE, second, 2036, 12, 2048, b[2036:])
    send(session, "floats", message.BULK_COMMIT, second, 0, 0, 2048)
    send(session, "integers", message.BULK_COMMIT, first, 0, 0, 2048)
    assert session.read_bulk("data", "integers") == a
    assert session.read_bulk("data", "floats") == b
    session.write_bulk("data", "integers", [777])
    time.sleep(0.03)
    send(session, "integers", message.BULK_COMMIT, first, 0, 0, 2048)
    assert session.read_bulk("data", "integers") == [777, *a[1:]]


def test_snapshot_is_fixed_while_algorithm_updates(session):
    transfer = session._next_transfer_id()
    total, _ = send(session, "snapshot", message.BULK_READ, transfer, 0, 0, 0)
    assert total == 2048
    _, head = send(session, "snapshot", message.BULK_READ, transfer, 0, 1018, total)
    time.sleep(0.05)
    _, tail = send(session, "snapshot", message.BULK_READ, transfer, 1018, 1018, total)
    _, end = send(session, "snapshot", message.BULK_READ, transfer, 2036, 12, total)
    snapshot = message.decode_bulk("float", head + tail + end)
    assert len(snapshot) == 2048 and len(set(snapshot)) == 1
    live = session.read_bulk("data", "snapshot")
    assert len(set(live)) == 1 and live[0] != snapshot[0]
    send(session, "snapshot", message.BULK_CANCEL, transfer, 0, 0, total)
    assert session.poll_observations() == 2
    assert len(session.snapshot()["probes"]["data"]["snapshot"]) == 2048


def test_concurrent_api_calls_interleave_without_merging_transactions(session, monkeypatch):
    barrier = threading.Barrier(2)
    original = session._bulk_fragment

    def fragmented(*args, **kwargs):
        response = original(*args, **kwargs)
        if args[1] == message.BULK_WRITE and args[3] == 0:
            barrier.wait(timeout=3)
        return response

    monkeypatch.setattr(session, "_bulk_fragment", fragmented)
    with ThreadPoolExecutor(max_workers=2) as pool:
        first = pool.submit(session.write_bulk, "data", "integers", [-123456789] * 2048)
        second = pool.submit(session.write_bulk, "data", "floats", [7.25] * 2048)
        first.result(timeout=5)
        second.result(timeout=5)
    assert session.read_bulk("data", "integers") == [-123456789] * 2048
    assert session.read_bulk("data", "floats") == [7.25] * 2048


def test_cancel_releases_array_and_raw_msg_keeps_fragment_flag(session):
    transfer = session._next_transfer_id()
    payload = message.BULK_FRAGMENT_HEADER.pack(message.BULK_WRITE, transfer, 0, 1, 2048)
    frame = message.make_frame(data_id(session, "integers"), 900, payload=payload + b"\x01\0\0\0",
                               flags=message.FLAG_FRAGMENT)
    reply = message.parse_frame(bytes.fromhex(session.msg(frame.hex(), 900)))
    assert not reply["error"] and reply["flags"] == message.FLAG_FRAGMENT
    send(session, "integers", message.BULK_CANCEL, transfer, 0, 0, 0)
    send(session, "integers", message.BULK_CANCEL, transfer, 0, 0, 0)
    assert session.read_bulk("data", "integers") == [0] * 2048
    session.write_bulk("data", "integers", [123] * 2048)
    assert session.read_bulk("data", "integers") == [123] * 2048


def test_minimum_fragment_frame_and_word_padding(session):
    negotiated = session.capabilities
    session.capabilities = replace(negotiated, max_frame=32)
    session.write_bulk("data", "small", list(range(17)))
    assert session.read_bulk("data", "small") == list(range(17))
    session.capabilities = replace(negotiated, max_frame=31)
    with pytest.raises(RuntimeError, match="32字节"):
        session.read_bulk("data", "small")
    with pytest.raises(RuntimeError, match="max_frame"):
        session.call(data_id(session, "integers"), b"x" * 23)


@pytest.mark.parametrize("damage", ["transfer", "offset", "total", "flags", "short", "extra"])
def test_fragment_response_metadata_is_checked(session, monkeypatch, damage):
    fields = [message.BULK_READ, 123, 0, 1, 2048]
    flags = message.FLAG_FRAGMENT
    if damage == "transfer":
        fields[1] += 1
    elif damage == "offset":
        fields[2] += 1
    elif damage == "total":
        fields[4] -= 1
    elif damage == "flags":
        flags = 0
    payload = message.BULK_FRAGMENT_HEADER.pack(*fields) + b"\0" * 4
    if damage == "short":
        payload = payload[:16]
    elif damage == "extra":
        payload += b"\0" * 4
    monkeypatch.setattr(session, "call", lambda *args, **kwargs: {
        "flags": flags, "payload": payload, "error": False,
    })
    with pytest.raises(RuntimeError, match="分片"):
        session._bulk_fragment(data_id(session, "integers"), message.BULK_READ, 123, 0, 1, 2048)


def test_open_read_timeout_cancels_even_without_confirmed_total(session, monkeypatch):
    calls = []

    def busy(route, payload, **kwargs):
        fields = list(message.BULK_FRAGMENT_HEADER.unpack_from(payload))
        calls.append(tuple(fields))
        fields[4] = 2048
        is_read = fields[0] == message.BULK_READ
        return {
            "flags": message.FLAG_FRAGMENT | (message.FLAG_ERROR if is_read else 0),
            "payload": message.BULK_FRAGMENT_HEADER.pack(*fields) +
            (message.BULK_BUSY.to_bytes(4, "little") if is_read else b""),
            "error": is_read,
        }

    monkeypatch.setattr(session, "call", busy)
    session.bulk_timeout = 0.005
    with pytest.raises(RuntimeError, match="超时"):
        session.read_bulk("data", "integers")
    assert calls[-1][0] == message.BULK_CANCEL and calls[-1][4] == 0
    assert calls[-1][1] == calls[0][1]


def test_failed_write_cancels_staging_without_committing(session, monkeypatch):
    operations = []

    def fail_second(data_id, operation, transfer, offset, count, total, data=b""):
        operations.append(operation)
        if operation == message.BULK_WRITE and offset > 0:
            raise RuntimeError("模拟分片传输失败")
        return total, b""

    monkeypatch.setattr(session, "_bulk_fragment", fail_second)
    with pytest.raises(RuntimeError, match="模拟分片传输失败"):
        session.write_bulk("data", "integers", [123] * 2048)
    assert operations == [message.BULK_WRITE, message.BULK_WRITE, message.BULK_CANCEL]


def test_fragment_flag_cannot_turn_scalar_probe_into_bulk(session):
    payload = message.BULK_FRAGMENT_HEADER.pack(
        message.BULK_READ, session._next_transfer_id(), 0, 0, 0,
    )
    response = session.call(data_id(session, "scalar"), payload, flags=message.FLAG_FRAGMENT)
    assert response["error"] and response["flags"] == message.FLAG_FRAGMENT | message.FLAG_ERROR
    assert int.from_bytes(response["payload"][20:], "little") == 1
