"""统一 BridgeSession：半双工基线与全双工能力升级。"""

from __future__ import annotations

import json
import math
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
    bulk_fragment: bool = False

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
        probe_interval: float = 0.0,
        bulk_timeout: float = 30.0,
    ):
        if not math.isfinite(bulk_timeout) or bulk_timeout <= 0:
            raise ValueError("BULK 分片等待超时必须为有限正数")
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
        self.bulk_timeout = bulk_timeout
        self._read_chunk = read_chunk
        self._log_sink = log_sink or NullLogSink()
        self._probe_interval = max(0.0, probe_interval)

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
        self._transfer_sequence = 0
        self._closed = threading.Event()
        self._probe_stop = threading.Event()
        self._link_errors = 0
        self.hello: dict[str, Any] | None = None
        self.identity: dict[str, Any] | None = None
        self.identity_verified = False
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._log(
            f"Bridge 会话已建立：duplex={self.capabilities.duplex.value}, "
            f"pipelined_calls={int(self.capabilities.pipelined_calls)}"
        )
        self._reader.start()
        self._probe_thread: threading.Thread | None = None

    def _log(self, line: str) -> None:
        with self._condition:
            self._logs.append(line)
        self._log_sink.emit(line)

    @property
    def running(self) -> bool:
        transport_running = getattr(self.transport, "running", True)
        return not self._closed.is_set() and bool(transport_running)

    def close(self) -> None:
        if self._closed.is_set():
            return
        self._probe_stop.set()
        self._closed.set()
        try:
            self.transport.close()
        finally:
            with self._condition:
                self._condition.notify_all()
            self._reader.join(timeout=max(self.call_timeout * 2, 0.2))
            if self._probe_thread is not None:
                self._probe_thread.join(timeout=max(self.call_timeout * 2, 0.2))
            self._log_sink.close()

    def stop(self, timeout: float = 3.0) -> None:  # noqa: ARG002
        if self.running and self.hello is not None:
            try:
                self.stop_endpoint()
            except Exception as exc:
                self._log(f"Bridge STOP 失败，关闭 Transport: {exc}")
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
                if not self.running:
                    with self._condition:
                        self._condition.notify_all()
                    return
                continue
            for frame in self.codec.feed(data):
                try:
                    self._dispatch(frame)
                except Exception as exc:
                    self._link_errors += 1
                    self._log(f"Bridge 帧分发错误: {exc}")

    def _poll_loop(self) -> None:
        while not self._probe_stop.wait(self._probe_interval):
            if not self.running:
                return
            self.poll_observations()

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
                if entry["type"] == "string":
                    try:
                        value = json.loads(value)
                    except json.JSONDecodeError:
                        pass
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
                if entry["form"] == "BULK":
                    value = self.read_bulk(data_id=entry["id"])
                    with self._condition:
                        self._probes.setdefault(entry["node"], {})[entry["key"]] = value
                    updated += 1
                    continue
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
        flags: int = 0,
    ) -> dict[str, Any]:
        """发送 CALL 并等待匹配 RESPONSE；半双工基线串行化全部 CALL。"""
        if not self.running:
            raise RuntimeError("Bridge 会话已关闭")
        if 8 + ((len(payload) + 3) // 4) * 4 > min(self.capabilities.max_frame, 4100):
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
                frame = message.make_frame(route, call_id, message.CALL, payload, flags)
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
                                if parsed["route"] != route:
                                    raise RuntimeError("Bridge RESPONSE route 与 CALL 不一致")
                                parsed["raw"] = response
                                return parsed
                            remaining = deadline - time.time()
                            if remaining <= 0 or not self.running:
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

    def connect(self, *, expected_id_map_hash: int | None = None) -> dict[str, Any]:
        """执行标准 HELLO/IDENTITY；hash 不一致时保持只读。"""
        hello_response = self.call(message.ROUTE_HELLO)
        self._check_ok(hello_response, "HELLO")
        hello = message.decode_hello(hello_response["payload"])
        if hello["protocol_version"] != message.BRIDGE_PROTOCOL_VERSION:
            raise RuntimeError(
                f"Bridge 协议版本不匹配: endpoint={hello['protocol_version']} "
                f"host={message.BRIDGE_PROTOCOL_VERSION}"
            )
        capability_bits = hello["capabilities"]
        duplex = (
            DuplexMode.FULL
            if capability_bits & message.CAP_FULL_DUPLEX
            else DuplexMode.HALF
        )
        self.capabilities = BridgeCapabilities(
            duplex=duplex,
            unsolicited=bool(capability_bits & message.CAP_UNSOLICITED),
            pipelined_calls=bool(capability_bits & message.CAP_PIPELINED_CALLS),
            flow_control=bool(capability_bits & message.CAP_FLOW_CONTROL),
            max_frame=hello["max_message"],
            bulk_fragment=bool(capability_bits & message.CAP_BULK_FRAGMENT),
        )
        identity_response = self.call(message.ROUTE_IDENTITY)
        self._check_ok(identity_response, "IDENTITY")
        identity = message.decode_identity(identity_response["payload"])
        self.hello = hello
        self.identity = identity
        self.identity_verified = (
            expected_id_map_hash is None
            or identity["id_map_hash"] == expected_id_map_hash
        )
        if (self._probe_thread is None and self._probe_interval > 0
                and not self.capabilities.unsolicited):
            self._probe_thread = threading.Thread(target=self._poll_loop, daemon=True)
            self._probe_thread.start()
        return {"hello": hello, "identity": identity,
                "identity_verified": self.identity_verified}

    def _require_write_access(self) -> None:
        if self.identity is None:
            raise RuntimeError("Bridge 尚未完成 HELLO/IDENTITY")
        if not self.identity_verified:
            raise RuntimeError("Bridge id_map hash 不匹配，写操作已禁止")
        if not self.identity["flags"] & message.IDENTITY_WRITABLE:
            raise RuntimeError("Bridge Endpoint 为只读")

    def stop_endpoint(self) -> None:
        if self.hello is None:
            raise RuntimeError("Bridge 尚未完成 HELLO/IDENTITY")
        response = self.call(message.ROUTE_STOP)
        self._check_ok(response, "STOP")

    def bridge_stats(self) -> dict[str, int]:
        response = self.call(message.ROUTE_STATS)
        self._check_ok(response, "STATS")
        return message.decode_stats(response["payload"])

    def map_page(self, start: int = 0, limit: int = 0) -> dict[str, Any]:
        response = self.call(
            message.ROUTE_MAP, message.encode_map_request(start, limit))
        self._check_ok(response, "MAP")
        return message.decode_map_page(response["payload"])

    def set_parameter(self, node: str, param: str, value: Any) -> None:
        self._require_write_access()
        entry = self._entry(node, param)
        if entry["kind"] in ("PROBE", "STATE"):
            raise RuntimeError(f"{node}.{param} 为只读（{entry['kind']}）")
        response = self.call(
            entry["id"], message.encode_scalar(entry["type"], value)
        )
        self._check_ok(response, f"SET {node}.{param}")

    def write_id(self, data_id: int, value: Any) -> None:
        self._require_write_access()
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

    def _bulk_entry(self, data_id: int) -> dict[str, Any]:
        entry = self._by_id.get(data_id)
        if entry is None:
            raise RuntimeError(f"BULK ID未登记，不能猜测元素类型：0x{data_id:08x}")
        if entry.get("form") != "BULK":
            raise RuntimeError(f"数据项不是BULK：0x{data_id:08x}")
        if entry.get("type") not in ("int", "float"):
            raise RuntimeError(f"BULK暂不支持元素类型：{entry.get('type')}")
        if type(entry.get("count")) is not int or entry["count"] < 1:
            raise RuntimeError(f"BULK元素数量声明无效：0x{data_id:08x}")
        return entry

    def write_bulk(self, node: str, key: str, values: list[int | float]) -> None:
        self._require_write_access()
        self.write_bulk_id(self._entry(node, key)["id"], values)

    def write_bulk_id(self, data_id: int, values: list[int | float]) -> None:
        self._require_write_access()
        entry = self._bulk_entry(data_id)
        if len(values) > entry["count"]:
            raise RuntimeError(f"BULK写入超过声明容量：{len(values)} > {entry['count']}")
        payload = message.encode_bulk(entry["type"], values)
        if len(payload) + 8 > min(self.capabilities.max_frame, 4100):
            chunk_size = self._bulk_chunk_size()
            transfer_id = self._next_transfer_id()
            committed = False
            try:
                for offset in range(0, len(values), chunk_size):
                    count = min(chunk_size, len(values) - offset)
                    self._bulk_fragment(
                        data_id, message.BULK_WRITE, transfer_id, offset, count, len(values),
                        payload[offset * 4:(offset + count) * 4],
                    )
                self._bulk_fragment(
                    data_id, message.BULK_COMMIT, transfer_id, 0, 0, len(values),
                )
                committed = True
            finally:
                if not committed:
                    self._cancel_bulk(data_id, transfer_id, len(values))
            return
        response = self.call(data_id, payload)
        self._check_ok(response, f"WRITE_BULK 0x{data_id:08x}")

    def read_bulk(
        self,
        node: str | None = None,
        key: str | None = None,
        data_id: int | None = None,
    ) -> list[int | float]:
        if data_id is None:
            if node is None or key is None:
                raise RuntimeError("读取 BULK 需要 data_id 或 node/key")
            data_id = self._entry(node, key)["id"]
        entry = self._bulk_entry(data_id)
        oversized = entry["count"] * 4 + 8 > min(self.capabilities.max_frame, 4100)
        if oversized and self.capabilities.bulk_fragment:
            chunk_size = self._bulk_chunk_size()
            transfer_id = self._next_transfer_id()
            total = 0
            try:
                total, _ = self._bulk_fragment(
                    data_id, message.BULK_READ, transfer_id, 0, 0, 0,
                )
                if total < 1 or total > entry["count"]:
                    raise RuntimeError("BULK 快照长度超过声明容量")
                chunks = []
                for offset in range(0, total, chunk_size):
                    _, chunk = self._bulk_fragment(
                        data_id, message.BULK_READ, transfer_id, offset,
                        min(chunk_size, total - offset), total,
                    )
                    chunks.append(chunk)
                return message.decode_bulk(entry["type"], b"".join(chunks))
            finally:
                self._cancel_bulk(data_id, transfer_id, total)
        response = self.call(data_id)
        if oversized and response["error"] and not self.capabilities.bulk_fragment:
            raise RuntimeError("Endpoint 未协商 BULK 分片能力，不能读取超限数组")
        self._check_ok(response, f"READ_BULK 0x{data_id:08x}")
        payload = response["payload"]
        if len(payload) > entry["count"] * 4:
            raise RuntimeError(f"BULK响应超过声明容量：0x{data_id:08x}")
        return message.decode_bulk(entry["type"], payload)

    def _bulk_chunk_size(self) -> int:
        if not self.capabilities.bulk_fragment:
            raise RuntimeError("Endpoint 未协商 BULK 分片能力，不能传输超限数组")
        if self.capabilities.max_frame < 32:
            raise RuntimeError("BULK 分片需要 max_frame 至少为32字节")
        return (min(self.capabilities.max_frame, 4100) - 28) // 4

    def _next_transfer_id(self) -> int:
        with self._call_id_lock:
            if self._transfer_sequence >= 0xFFFFFFFF:
                raise RuntimeError("BULK transfer_id 已耗尽，需要新建 Bridge 会话")
            self._transfer_sequence += 1
            return self._transfer_sequence

    def _bulk_fragment(
        self, data_id: int, operation: int, transfer_id: int, offset: int,
        count: int, total: int, data: bytes = b"",
    ) -> tuple[int, bytes]:
        header = message.BULK_FRAGMENT_HEADER.pack(operation, transfer_id, offset, count, total)
        deadline = time.monotonic() + self.bulk_timeout
        while True:
            response = self.call(data_id, header + data, flags=message.FLAG_FRAGMENT)
            payload = response["payload"]
            if (response["flags"] & ~message.FLAG_ERROR) != message.FLAG_FRAGMENT or len(payload) < 20:
                raise RuntimeError("BULK 分片响应缺少分片标记或扩展头")
            echoed = message.BULK_FRAGMENT_HEADER.unpack_from(payload)
            if echoed[:4] != (operation, transfer_id, offset, count) or (
                echoed[4] != total and not (
                    operation in (message.BULK_READ, message.BULK_CANCEL) and total == 0
                )
            ):
                raise RuntimeError("BULK 分片响应的传输标识、范围或总长度不匹配")
            if response["error"]:
                if len(payload) != 24:
                    raise RuntimeError("BULK 分片错误响应长度无效")
                code = struct.unpack_from("<I", payload, 20)[0]
                if code == message.BULK_BUSY and operation != message.BULK_WRITE:
                    if time.monotonic() >= deadline:
                        raise RuntimeError(f"BULK transfer_id={transfer_id} 等待块边界超时")
                    time.sleep(0.002)
                    continue
                reason = message.BULK_ERRORS.get(code, "未知分片错误")
                raise RuntimeError(f"BULK transfer_id={transfer_id} 失败：{reason}（{code}）")
            expected_size = 20 + (count * 4 if operation == message.BULK_READ else 0)
            if len(payload) != expected_size:
                raise RuntimeError("BULK 分片响应的数据长度不匹配")
            return echoed[4], payload[20:]

    def _cancel_bulk(self, data_id: int, transfer_id: int, total: int) -> None:
        try:
            self._bulk_fragment(data_id, message.BULK_CANCEL, transfer_id, 0, 0, total)
        except RuntimeError as exc:
            self._log(f"BULK 事务清理失败（Endpoint 将超时回收）：{exc}")

    def msg(self, msg_hex: str, call_id: int) -> str:
        self._require_write_access()
        frame = bytes.fromhex(msg_hex)
        parsed = message.parse_frame(frame)
        if parsed["type"] == message.NOTIFICATION:
            self._write_all(self.codec.encode(frame))
            return ""
        response = self.call(
            parsed["route"], parsed["payload"], call_id=call_id, flags=parsed["flags"]
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
                "bulk_fragment": self.capabilities.bulk_fragment,
                "max_frame": self.capabilities.max_frame,
                "link_errors": self._link_errors,
                "log_sink": self._log_sink.stats(),
            },
        }
