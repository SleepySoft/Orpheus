"""SerialSession（L4 串口设备会话）测试：内存管道 + 模拟设备，不依赖真实串口。

模拟设备行为对齐生成代码 orpheus_control_message 语义：
CALL 写（有 payload）/读（无 payload）→ RESPONSE；CUSTOM/只读拒写 → ERROR flag；
可注入丢帧（测试超时重发）与主动 NOTIFICATION（探针上行）。
"""
from __future__ import annotations

import queue
import struct
import threading
import time

import pytest

from orpheus_core.link import message, olink
from orpheus_core.server.serial_session import SerialSession

# 模拟设备的 id_map（对齐 compiler 产出形状：kind/form 大写字符串）
GAIN_ID = 0x00040002
RMS_ID = 0x20040003
COEFS_ID = 0x10050008
ID_MAP = [
    {"id": GAIN_ID, "node": "front__trim", "key": "gain_db", "kind": "RTC",
     "form": "SCALAR", "type": "float", "count": 1, "name": "增益"},
    {"id": RMS_ID, "node": "front__mon", "key": "rms", "kind": "PROBE",
     "form": "SCALAR", "type": "float", "count": 1, "name": "电平"},
    {"id": COEFS_ID, "node": "front__eq__bq", "key": "bq0.coefs", "kind": "TUNE",
     "form": "BULK", "type": "float", "count": 5, "name": "系数"},
]


class PipeTransport:
    """会话侧传输：write 进设备 decoder，设备应答进 rx 队列供 read。"""

    def __init__(self):
        self.rx: queue.Queue[bytes] = queue.Queue()
        self.on_write = None  # 设备侧回调
        self.closed = False

    def write(self, data: bytes) -> int:
        if self.on_write:
            self.on_write(data)
        return len(data)

    def read(self, n: int) -> bytes:  # noqa: ARG002
        try:
            return self.rx.get(timeout=0.05)
        except queue.Empty:
            return b""

    def close(self) -> None:
        self.closed = True


class FakeDevice:
    """极简生成代码侧：id->4 字节标量 / bulk 数组，CALL→RESPONSE。"""

    def __init__(self, transport: PipeTransport):
        self.t = transport
        self.decoder = olink.Decoder()
        self.scalars = {GAIN_ID: struct.pack("<f", 0.0)}
        self.bulks = {COEFS_ID: [1.0, 0.0, 0.0, 0.0, 0.0]}
        self.drop_next = 0  # 丢弃接下来 N 个 CALL（模拟线路丢帧）
        transport.on_write = self._on_bytes

    def _on_bytes(self, data: bytes) -> None:
        for frame in self.decoder.feed(data):
            rsp = self._handle(frame)
            if rsp is not None:
                self.t.rx.put(olink.encode(rsp))

    def _handle(self, frame: bytes) -> bytes | None:
        msg = message.parse_frame(frame)
        if self.drop_next > 0:
            self.drop_next -= 1
            return None
        route, cid, payload = msg["route"], msg["call_id"], msg["payload"]
        write = len(payload) > 0
        error = False
        resp_payload = b""
        if route in self.scalars:
            if write:
                self.scalars[route] = payload[:4]
            else:
                resp_payload = self.scalars[route]
        elif route in self.bulks:
            if write:
                n = len(payload) // 4
                vals = list(struct.unpack(f"<{n}f", payload))
                if n > len(self.bulks[route]):
                    error = True
                else:
                    self.bulks[route][:n] = vals
            else:
                resp_payload = struct.pack(f"<{len(self.bulks[route])}f", *self.bulks[route])
        elif route == RMS_ID:
            error = write  # PROBE 只读
        else:
            error = True
        flags = message.FLAG_ERROR if error else 0
        return message.make_frame(route, cid, message.RESPONSE, resp_payload, flags)

    def emit_probe(self, value: float) -> None:
        frame = message.make_frame(RMS_ID, 0, message.NOTIFICATION, struct.pack("<f", value))
        self.t.rx.put(olink.encode(frame))


@pytest.fixture()
def session():
    t = PipeTransport()
    dev = FakeDevice(t)
    s = SerialSession(t, ID_MAP, call_timeout=0.15, call_retries=1)
    yield s, dev
    s.close()


def test_write_then_read_scalar(session):
    s, dev = session
    s.write_id(GAIN_ID, -6.0)
    assert struct.unpack("<f", dev.scalars[GAIN_ID])[0] == pytest.approx(-6.0)
    assert s.read_id(GAIN_ID) == pytest.approx(-6.0)


def test_set_parameter_by_node_key(session):
    s, dev = session
    s.set_parameter("front__trim", "gain_db", 3.5)
    assert struct.unpack("<f", dev.scalars[GAIN_ID])[0] == pytest.approx(3.5)


def test_set_parameter_rejects_probe(session):
    s, _ = session
    with pytest.raises(RuntimeError, match="只读"):
        s.set_parameter("front__mon", "rms", 1.0)


def test_bulk_write_read(session):
    s, dev = session
    s.write_bulk("front__eq__bq", "bq0.coefs", [1.0, 2.0, 3.0, 4.0, 5.0])
    assert dev.bulks[COEFS_ID] == [1.0, 2.0, 3.0, 4.0, 5.0]
    assert s.read_bulk(node="front__eq__bq", key="bq0.coefs") == [1.0, 2.0, 3.0, 4.0, 5.0]
    assert s.read_bulk(data_id=COEFS_ID) == [1.0, 2.0, 3.0, 4.0, 5.0]


def test_msg_passthrough_call_id_match(session):
    s, _ = session
    frame = message.make_call(GAIN_ID, 0x1234, struct.pack("<f", -1.0))
    rsp_hex = s.msg(frame.hex(), 0x1234)
    rsp = message.parse_frame(bytes.fromhex(rsp_hex))
    assert rsp["type"] == message.RESPONSE and rsp["call_id"] == 0x1234
    assert rsp["route"] == GAIN_ID and not rsp["error"]


def test_notification_feeds_probe_cache(session):
    s, dev = session
    dev.emit_probe(0.432)
    deadline = time.time() + 1.0
    while time.time() < deadline:
        snap = s.snapshot()
        if snap["probes"].get("front__mon", {}).get("rms") is not None:
            break
        time.sleep(0.02)
    assert s.snapshot()["probes"]["front__mon"]["rms"] == pytest.approx(0.432)


def test_timeout_retry_succeeds(session):
    s, dev = session
    dev.drop_next = 1  # 第一帧丢失，重发应成功
    assert s.read_id(GAIN_ID) == pytest.approx(0.0)
    assert any("重发" in line for line in s.snapshot()["logs"])


def test_timeout_exhausted_raises(session):
    s, dev = session
    dev.drop_next = 99
    with pytest.raises(RuntimeError, match="无响应"):
        s.read_id(GAIN_ID)


def test_device_error_flag_raises(session):
    s, _ = session
    with pytest.raises(RuntimeError, match="ERROR"):
        s.write_id(RMS_ID, 1.0)  # PROBE 拒写


def test_resolve_and_map_local(session):
    s, _ = session
    r = s.resolve(GAIN_ID)
    assert r["kind"] == "RTC" and r["node"] == "front__trim" and r["base"] == "device"
    assert len(s.map_all()) == len(ID_MAP)
    with pytest.raises(RuntimeError, match="未知数据 ID"):
        s.resolve(0x7FFFFFFF)


def test_snapshot_shape(session):
    s, _ = session
    snap = s.snapshot()
    assert set(snap) == {"running", "exit_code", "started_at", "logs", "probes"}
    assert snap["running"] is True and snap["exit_code"] is None
    s.stop()
    assert s.snapshot()["running"] is False


# ------------------------------------------------------------------ REST 端点


def test_link_ports_endpoint_shape():
    from fastapi.testclient import TestClient
    from orpheus_core.server.app import create_app
    from pathlib import Path

    root = Path(__file__).resolve().parents[2]
    with TestClient(create_app(root)) as client:
        r = client.get("/api/link/ports")
        assert r.status_code == 200
        body = r.json()
        assert "ports" in body
        for p in body["ports"]:
            assert set(p) == {"device", "description", "hwid"}


def test_rt_start_serial_requires_port():
    import uuid
    from fastapi.testclient import TestClient
    from orpheus_core.server.app import create_app
    from pathlib import Path

    root = Path(__file__).resolve().parents[2]
    name = f"ser_{uuid.uuid4().hex[:8]}"
    with TestClient(create_app(root)) as client:
        try:
            assert client.post("/api/projects", json={"name": name}).status_code == 201
            r = client.post(f"/api/projects/{name}/rt/start", json={"target": "serial"})
            assert r.status_code == 400 and "port" in r.json()["detail"]
        finally:
            client.delete(f"/api/projects/{name}")
