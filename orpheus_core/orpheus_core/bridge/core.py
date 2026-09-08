"""统一 BridgeSession：半双工基线与全双工能力升级。"""

from __future__ import annotations

import struct
import threading
import time
from collections import deque
from contextlib import nullcontext
from dataclasses import dataclass
from enum import Enum
from typing import Any

from orpheus_core.bridge.codec import FrameCodec
from orpheus_core.bridge.log_sink import LogSink, NullLogSink
from orpheus_core.bridge.transport import ByteTransport
from orpheus_core.link import message

MAX_LOG_LINES = 500


class DuplexMode(str, Enum):
    HALF = "half"
    FULL = "full"


@dataclass(frozen=True)
class BridgeCapabilities:
    """协商后的会话能力；半双工且单 outstanding CALL 是最低基线。"""

    duplex: DuplexMode = DuplexMode.HALF
    unsolicited: bool = False
    pipelined_calls: bool = False
    flow_control: bool = False
    max_frame: int = 8 + 1023 * 4

    def __post_init__(self) -> None:
        if self.max_frame < 8:
            raise ValueError("Bridge max_frame 不能小于消息头长度")
        if self.unsolicited and self.duplex is not DuplexMode.FULL:
            raise ValueError("主动 NOTIFICATION 需要全双工 Transport")
        if self.pipelined_calls and self.duplex is not DuplexMode.FULL:
            raise ValueError("流水化 CALL 需要全双工 Transport")


class BridgeSession:
    """与 Transport/Codec 解耦的统一主机侧访问会话。"""

    def __init__(
        self,
        transport: ByteTransport,
        codec: FrameCodec,
        id_map: list[dict[str, Any]],
        *,
        capabilities: BridgeCapabilities | None = None,
        call_timeout: float = 0.3,
        call_retries: int = 2,
        read_chunk: int = 4096,
        log_sink: LogSink | None = None,
    ):
        self.transport = transport
        self.codec = codec
        self.capabilities = capabilities or BridgeCapabilities()
        self._id_map = list(id_map)
        self._by_id = {entry["id"]: entry for entry in self._id_map}
        self._by_node_key = {
            (entry["node"], entry["key"]): entry for entry in self._id_map
        }
        self.call_timeout = call_timeout
        self.call_retries = call_retries
        self._read_chunk = read_chunk
        self._log_sink = log_sink or NullLogSink()

        self.started_at = time.time()
        self._logs: deque[str] = deque(maxlen=MAX_LOG_LINES)
        self._probes: dict[str, dict[str, Any]] = {}
        self._pending: dict[int, bytes] = {}
        self._inflight: set[int] = set()
        self._condition = threading.Condition()
        self._call_lock = threading.Lock()
        self._call_id_lock = threading.Lock()
        self._transport_write_lock = threading.Lock()
        self._call_sequence = 0
        self._closed = threading.Event()
        self._link_errors = 0
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._log(
            f"Bridge 会话已建立：duplex={self.capabilities.duplex.value}, "
            f"pipelined_calls={int(self.capabilities.pipelined_calls)}"
        )
        self._reader.start()

    def _log(self, line: str) -> None:
        with self._condition:
            self._logs.append(line)
        self._log_sink.emit(line)

    @property
    def running(self) -> bool:
        return not self._closed.is_set()

    def close(self) -> None:
        if self._closed.is_set():
            return
        self._closed.set()
        try:
            self.transport.close()
        finally:
            with self._condition:
                self._condition.notify_all()
            self._reader.join(timeout=max(self.call_timeout * 2, 0.2))
            self._log_sink.close()

    def stop(self, timeout: float = 3.0) -> None:  # noqa: ARG002
        self.close()

    def _read_loop(self) -> None:
        while not self._closed.is_set():
            try:
                data = self.transport.read(self._read_chunk)
            except Exception as exc:
                if self._closed.is_set():
                    break
                self._link_errors += 1
                self._log(f"Bridge 读取错误: {exc}")
                time.sleep(0.2)
                continue
            if not data:
                continue
            for frame in self.codec.feed(data):
                try:
                    self._dispatch(frame)
                except Exception as exc:
                    self._link_errors += 1
                    self._log(f"Bridge 帧分发错误: {exc}")

    def _dispatch(self, frame: bytes) -> None:
        parsed = message.parse_frame(frame)
        if parsed["type"] == message.RESPONSE:
            call_id = parsed["call_id"]
            unmatched = False
            with self._condition:
                if call_id not in self._inflight:
                    unmatched = True
                else:
                    self._pending[call_id] = frame
                    self._condition.notify_all()
            if unmatched:
                self._log(f"忽略无匹配 CALL 的 RESPONSE call_id={call_id}")
        elif parsed["type"] == message.NOTIFICATION:
            if not self.capabilities.unsolicited:
                self._log(
                    "忽略未协商的主动 NOTIFICATION "
                    f"route=0x{parsed['route']:08x}"
                )
                return
            self._handle_notification(parsed)
        else:
            self._log(
                f"忽略非预期帧 type={parsed['type']} route=0x{parsed['route']:08x}"
            )

    def _handle_notification(self, parsed: dict[str, Any]) -> None:
        entry = self._by_id.get(parsed["route"])
        if entry is None or entry["kind"] != "PROBE":
            self._log(f"未知 NOTIFICATION route=0x{parsed['route']:08x}")
            return
        payload = parsed["payload"]
        try:
            if entry["form"] == "BULK" or entry.get("count", 1) > 1:
                count = min(entry.get("count", 1), len(payload) // 4)
                value = list(struct.unpack(f"<{count}f", payload[: count * 4]))
            else:
                value = message.decode_scalar(entry["type"], payload)
        except (struct.error, IndexError):
            self._log(f"探针帧 payload 长度异常: 0x{parsed['route']:08x}")
            return
        with self._condition:
            self._probes.setdefault(entry["node"], {})[entry["key"]] = value

    def poll_observations(self) -> int:
        """按 Core Profile 串行读取全部 Probe，返回成功更新数量。"""
        updated = 0
        for entry in self._id_map:
            if entry["kind"] != "PROBE" or not self.running:
                continue
            try:
                response = self.call(entry["id"])
                self._check_ok(response, f"POLL 0x{entry['id']:08x}")
                parsed = {
                    "route": entry["id"],
                    "payload": response["payload"],
                }
                self._handle_notification(parsed)
                updated += 1
            except (RuntimeError, struct.error, IndexError) as exc:
                self._log(f"Probe 轮询失败 0x{entry['id']:08x}: {exc}")
        return updated

    def _next_call_id(self) -> int:
        with self._call_id_lock:
            for _ in range(0xFFFF):
                self._call_sequence = (self._call_sequence + 1) & 0xFFFF
                if self._call_sequence == 0:
                    self._call_sequence = 1
                if self._call_sequence not in self._inflight:
                    return self._call_sequence
        raise RuntimeError("Bridge call_id 已耗尽")

    def _write_all(self, data: bytes) -> None:
        with self._transport_write_lock:
            offset = 0
            while offset < len(data):
                written = self.transport.write(data[offset:])
                if written <= 0:
                    raise RuntimeError("Bridge Transport 未能写入数据")
                offset += written

    def call(
        self,
        route: int,
        payload: bytes = b"",
        *,
        call_id: int | None = None,
        timeout: float | None = None,
    ) -> dict[str, Any]:
        """发送 CALL 并等待匹配 RESPONSE；半双工基线串行化全部 CALL。"""
        if not self.running:
            raise RuntimeError("Bridge 会话已关闭")
        if len(payload) + 8 > self.capabilities.max_frame:
            raise RuntimeError("Bridge 消息超过协商的 max_frame")

        guard = nullcontext() if self.capabilities.pipelined_calls else self._call_lock
        with guard:
            call_id = call_id if call_id is not None else self._next_call_id()
            with self._condition:
                if call_id in self._inflight:
                    raise RuntimeError(f"Bridge call_id={call_id} 已在使用")
                self._inflight.add(call_id)
                self._pending.pop(call_id, None)
            try:
                frame = message.make_call(route, call_id, payload)
                wire = self.codec.encode(frame)
                attempts = self.call_retries + 1
                timeout_s = timeout if timeout is not None else self.call_timeout
                for attempt in range(attempts):
                    try:
                        self._write_all(wire)
                    except Exception as exc:
                        self._link_errors += 1
                        raise RuntimeError(f"Bridge 写入失败: {exc}") from exc
                    deadline = time.time() + timeout_s
                    with self._condition:
                        while True:
                            response = self._pending.pop(call_id, None)
                            if response is not None:
                                parsed = message.parse_frame(response)
                                parsed["raw"] = response
                                return parsed
                            remaining = deadline - time.time()
                            if remaining <= 0 or self._closed.is_set():
                                break
                            self._condition.wait(remaining)
                    if attempt + 1 < attempts:
                        self._log(
                            f"CALL 0x{route:08x} 超时，重发"
                            f"（{attempt + 1}/{self.call_retries}）"
                        )
                self._link_errors += 1
                raise RuntimeError(
                    f"CALL 0x{route:08x} 无响应（{attempts} 次尝试）"
                )
            finally:
                with self._condition:
                    self._inflight.discard(call_id)
                    self._pending.pop(call_id, None)

    @staticmethod
    def _check_ok(parsed: dict[str, Any], operation: str) -> None:
        if parsed["error"]:
            raise RuntimeError(f"{operation} 被 Endpoint 拒绝（ERROR flag）")

    def _entry(self, node: str, key: str) -> dict[str, Any]:
        entry = self._by_node_key.get((node, key))
        if entry is None:
            raise RuntimeError(f"数据点不存在: {node}.{key}")
        return entry

    def set_parameter(self, node: str, param: str, value: Any) -> None:
        entry = self._entry(node, param)
        if entry["kind"] in ("PROBE", "STATE"):
            raise RuntimeError(f"{node}.{param} 为只读（{entry['kind']}）")
        response = self.call(
            entry["id"], message.encode_scalar(entry["type"], value)
        )
        self._check_ok(response, f"SET {node}.{param}")

    def write_id(self, data_id: int, value: Any) -> None:
        entry = self._by_id.get(data_id)
        type_name = entry["type"] if entry else "float"
        response = self.call(data_id, message.encode_scalar(type_name, value))
        self._check_ok(response, f"WRITE 0x{data_id:08x}")

    def read_id(self, data_id: int) -> Any:
        entry = self._by_id.get(data_id)
        type_name = entry["type"] if entry else "float"
        response = self.call(data_id)
        self._check_ok(response, f"READ 0x{data_id:08x}")
        return message.decode_scalar(type_name, response["payload"])

    def write_bulk(self, node: str, key: str, values: list[float]) -> None:
        self.write_bulk_id(self._entry(node, key)["id"], values)

    def write_bulk_id(self, data_id: int, values: list[float]) -> None:
        payload = struct.pack(f"<{len(values)}f", *[float(value) for value in values])
        response = self.call(data_id, payload)
        self._check_ok(response, f"WRITE_BULK 0x{data_id:08x}")

    def read_bulk(
        self,
        node: str | None = None,
        key: str | None = None,
        data_id: int | None = None,
    ) -> list[float]:
        if data_id is None:
            if node is None or key is None:
                raise RuntimeError("读取 BULK 需要 data_id 或 node/key")
            data_id = self._entry(node, key)["id"]
        response = self.call(data_id)
        self._check_ok(response, f"READ_BULK 0x{data_id:08x}")
        payload = response["payload"]
        count = len(payload) // 4
        return list(struct.unpack(f"<{count}f", payload[: count * 4]))

    def msg(self, msg_hex: str, call_id: int) -> str:
        frame = bytes.fromhex(msg_hex)
        parsed = message.parse_frame(frame)
        if parsed["type"] == message.NOTIFICATION:
            self._write_all(self.codec.encode(frame))
            return ""
        response = self.call(
            parsed["route"], parsed["payload"], call_id=call_id
        )
        return response["raw"].hex()

    def resolve(self, data_id: int) -> dict[str, Any]:
        entry = self._by_id.get(data_id)
        if entry is None:
            raise RuntimeError(f"未知数据 ID: 0x{data_id:08x}")
        return {
            "id": data_id,
            "kind": entry["kind"],
            "form": entry["form"],
            "type": entry["type"],
            "count": entry["count"],
            "node": entry["node"],
            "key": entry["key"],
            "name": entry["name"],
            "base": "endpoint",
        }

    def map_all(self) -> list[dict[str, Any]]:
        return [self.resolve(entry["id"]) for entry in self._id_map]

    def snapshot(self, max_logs: int = 200) -> dict[str, Any]:
        with self._condition:
            logs = list(self._logs)[-max_logs:]
            probes = {node: dict(values) for node, values in self._probes.items()}
        return {
            "running": self.running,
            "exit_code": None,
            "started_at": self.started_at,
            "logs": logs,
            "probes": probes,
            "bridge": {
                "duplex": self.capabilities.duplex.value,
                "pipelined_calls": self.capabilities.pipelined_calls,
                "unsolicited": self.capabilities.unsolicited,
                "flow_control": self.capabilities.flow_control,
                "max_frame": self.capabilities.max_frame,
                "link_errors": self._link_errors,
                "log_sink": self._log_sink.stats(),
            },
        }
