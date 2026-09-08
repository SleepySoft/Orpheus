"""HLOS Bridge Adapter：stdio、TCP 与本地命名 Pipe 共用会话测试。"""

from __future__ import annotations

import io
import sys
import threading
import uuid
from pathlib import Path

import pytest

from orpheus_core.bridge import (
    BridgeCapabilities,
    BridgeSession,
    DuplexMode,
    LengthPrefixCodec,
    LocalPipeListener,
    LocalPipeTransport,
    ProcessTransport,
    StreamTransport,
    TcpListener,
    TcpTransport,
    TransportAdapterRegistry,
    default_hlos_adapters,
    local_pipe_address,
)
from bridge_echo_endpoint import VALUE_ROUTE, serve

ROOT = Path(__file__).resolve().parents[2]
ID_MAP = [{
    "id": VALUE_ROUTE,
    "node": "echo",
    "key": "value",
    "kind": "RTC",
    "form": "SCALAR",
    "type": "float",
    "count": 1,
    "name": "测试值",
}]


def exercise(transport) -> None:
    session = BridgeSession(
        transport,
        LengthPrefixCodec(),
        ID_MAP,
        call_timeout=1.0,
    )
    try:
        assert session.snapshot()["bridge"]["duplex"] == "half"
        session.write_id(VALUE_ROUTE, -7.25)
        assert session.read_id(VALUE_ROUTE) == pytest.approx(-7.25)
    finally:
        session.close()


def run_server(listener) -> threading.Thread:
    def target() -> None:
        transport = listener.accept()
        try:
            serve(transport)
        finally:
            transport.close()

    thread = threading.Thread(target=target, daemon=True)
    thread.start()
    return thread


def test_length_prefix_codec_handles_fragmented_stream() -> None:
    codec = LengthPrefixCodec()
    encoded = codec.encode(b"12345678") + codec.encode(b"abcdefgh")
    decoded: list[bytes] = []
    for byte in encoded:
        decoded.extend(codec.feed(bytes([byte])))
    assert decoded == [b"12345678", b"abcdefgh"]


def test_stream_transport_combines_separate_streams() -> None:
    reader = io.BytesIO(b"response")
    writer = io.BytesIO()
    transport = StreamTransport(reader, writer)
    assert transport.read(4) == b"resp"
    assert transport.write(b"request") == 7
    assert writer.getvalue() == b"request"
    assert transport.read(16) == b"onse"
    assert transport.read(1) == b""
    assert not transport.running


def test_process_stdio_transport() -> None:
    endpoint = Path(__file__).with_name("bridge_echo_endpoint.py")
    transport = ProcessTransport([sys.executable, str(endpoint)], cwd=ROOT)
    exercise(transport)
    assert not transport.running


def test_tcp_transport() -> None:
    with TcpListener() as listener:
        server = run_server(listener)
        host, port = listener.address
        exercise(TcpTransport.connect(host, port))
        server.join(timeout=2.0)
        assert not server.is_alive()


def test_tcp_transport_supports_full_duplex_pipelining() -> None:
    with TcpListener() as listener:
        server = run_server(listener)
        host, port = listener.address
        session = BridgeSession(
            TcpTransport.connect(host, port),
            LengthPrefixCodec(),
            ID_MAP,
            capabilities=BridgeCapabilities(
                duplex=DuplexMode.FULL,
                pipelined_calls=True,
            ),
            call_timeout=1.0,
        )
        results: list[float] = []
        calls = [
            threading.Thread(target=lambda: results.append(session.read_id(VALUE_ROUTE)))
            for _ in range(2)
        ]
        try:
            for call in calls:
                call.start()
            for call in calls:
                call.join()
            assert results == [pytest.approx(0.0), pytest.approx(0.0)]
        finally:
            session.close()
        server.join(timeout=2.0)
        assert not server.is_alive()


def test_local_named_pipe_transport(tmp_path) -> None:
    address = local_pipe_address(
        f"test-{uuid.uuid4().hex}", directory=tmp_path,
    )
    with LocalPipeListener(address) as listener:
        server = run_server(listener)
        exercise(LocalPipeTransport.connect(address))
        server.join(timeout=2.0)
        assert not server.is_alive()


def test_transport_registry_is_user_replaceable() -> None:
    registry = default_hlos_adapters()
    assert registry.names() == ("pipe", "process", "stdio", "tcp")

    custom = object()
    registry.register("vendor_ipc", lambda **config: (custom, config))
    assert registry.create("vendor_ipc", channel=3) == (custom, {"channel": 3})

    replacement = TransportAdapterRegistry()
    replacement.register("tcp", lambda **config: config)
    replacement.register("tcp", lambda **config: ("new", config), replace=True)
    assert replacement.create("tcp", host="lab") == ("new", {"host": "lab"})


def test_tcp_rest_session_lifecycle() -> None:
    from fastapi.testclient import TestClient
    from orpheus_core.server.app import create_app

    name = f"tcp_{uuid.uuid4().hex[:8]}"
    adapters = default_hlos_adapters()
    factory_calls: list[dict] = []

    def connect_tcp(**config):
        factory_calls.append(config)
        return TcpTransport.connect(**config)

    adapters.register("tcp", connect_tcp, replace=True)
    with TcpListener() as listener:
        server = run_server(listener)
        host, port = listener.address
        with TestClient(create_app(ROOT, transport_adapters=adapters)) as client:
            try:
                response = client.post(
                    "/api/projects",
                    json={"name": name, "from_example": "control_link_demo"},
                )
                assert response.status_code == 201
                response = client.post(
                    f"/api/projects/{name}/rt/start",
                    json={
                        "target": "tcp",
                        "host": host,
                        "network_port": port,
                        "duplex": "half",
                        "probe_interval_ms": 0,
                    },
                )
                assert response.status_code == 200
                assert response.json()["target"] == "tcp"
                assert factory_calls == [{"host": host, "port": port}]
                assert client.get(f"/api/projects/{name}/rt/status").json()["running"]
                assert client.post(f"/api/projects/{name}/rt/stop").status_code == 200
            finally:
                client.delete(f"/api/projects/{name}")
        server.join(timeout=2.0)
        assert not server.is_alive()


def test_local_pipe_rest_session_lifecycle(tmp_path) -> None:
    from fastapi.testclient import TestClient
    from orpheus_core.server.app import create_app

    name = f"pipe_{uuid.uuid4().hex[:8]}"
    address = local_pipe_address(f"api-{uuid.uuid4().hex}", directory=tmp_path)
    with LocalPipeListener(address) as listener:
        server = run_server(listener)
        with TestClient(create_app(ROOT)) as client:
            try:
                response = client.post(
                    "/api/projects",
                    json={"name": name, "from_example": "control_link_demo"},
                )
                assert response.status_code == 201
                response = client.post(
                    f"/api/projects/{name}/rt/start",
                    json={
                        "target": "pipe",
                        "pipe_address": address,
                        "duplex": "half",
                        "probe_interval_ms": 0,
                    },
                )
                assert response.status_code == 200
                assert response.json()["target"] == "pipe"
                assert client.get(f"/api/projects/{name}/rt/status").json()["running"]
                assert client.post(f"/api/projects/{name}/rt/stop").status_code == 200
            finally:
                client.delete(f"/api/projects/{name}")
        server.join(timeout=2.0)
        assert not server.is_alive()


def test_hlos_rest_target_validation() -> None:
    from fastapi.testclient import TestClient
    from orpheus_core.server.app import create_app

    name = f"hlos_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(ROOT)) as client:
        try:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            response = client.post(
                f"/api/projects/{name}/rt/start", json={"target": "tcp"},
            )
            assert response.status_code == 400
            assert "network_port" in response.json()["detail"]
            response = client.post(
                f"/api/projects/{name}/rt/start",
                json={"target": "pipe", "duplex": "quad"},
            )
            assert response.status_code == 400
            assert "half / full" in response.json()["detail"]
        finally:
            client.delete(f"/api/projects/{name}")
