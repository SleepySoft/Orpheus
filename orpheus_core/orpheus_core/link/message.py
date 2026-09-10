"""§18 消息信封的 Python 助手（与 orpheus_abi.h 位布局一致）。

8 字节头：route_id(LE u32) + bits(LE u32)。
bits: 31..30 type | 29..26 flags | 25..10 call_id | 9..0 payload_words。
"""

from __future__ import annotations

import struct

CALL, RESPONSE, NOTIFICATION = 0, 1, 2
FLAG_ERROR = 0x8  # flags 窗口 bit3（即 bits bit29）

BRIDGE_PROTOCOL_VERSION = 1
ROUTE_HELLO = 0xFFFF0001
ROUTE_IDENTITY = 0xFFFF0002
ROUTE_STOP = 0xFFFF0003
ROUTE_STATS = 0xFFFF0004
ROUTE_MAP = 0xFFFF0005

CAP_HALF_DUPLEX = 1 << 0
CAP_FULL_DUPLEX = 1 << 1
CAP_UNSOLICITED = 1 << 2
CAP_PIPELINED_CALLS = 1 << 3
CAP_FLOW_CONTROL = 1 << 4
CAP_MAP = 1 << 5
CAP_STOP = 1 << 6

IDENTITY_WRITABLE = 1 << 0

_KIND_NAMES = {0: "RTC", 1: "TUNE", 2: "PROBE", 3: "STATE", 4: "CUSTOM"}
_FORM_NAMES = {0: "SCALAR", 1: "BULK", 2: "MODULE"}


def kind_name(kind: int) -> str:
    return _KIND_NAMES.get(kind, str(kind))


def form_name(form: int) -> str:
    return _FORM_NAMES.get(form, str(form))


def make_frame(route: int, call_id: int, msg_type: int = CALL,
               payload: bytes = b"", flags: int = 0) -> bytes:
    words = (len(payload) + 3) // 4
    bits = (msg_type << 30) | (flags << 26) | (call_id << 10) | words
    return struct.pack("<II", route, bits) + payload.ljust(words * 4, b"\x00")


def make_call(route: int, call_id: int, payload: bytes = b"") -> bytes:
    return make_frame(route, call_id, CALL, payload)


def parse_frame(frame: bytes) -> dict:
    if len(frame) < 8:
        raise ValueError("消息过短（至少 8 字节头）")
    route, bits = struct.unpack("<II", frame[:8])
    words = bits & 0x3FF
    return {
        "route": route,
        "type": (bits >> 30) & 0x3,
        "flags": (bits >> 26) & 0xF,
        "call_id": (bits >> 10) & 0xFFFF,
        "payload": frame[8:8 + words * 4],
        "error": bool((bits >> 26) & 0xF & FLAG_ERROR),
    }


def encode_scalar(type_str: str, value) -> bytes:
    """标量写 payload（与 msg_default 的写路径一致：FLOAT/INT 4 字节，BOOL 1 字节）。"""
    if type_str == "string":
        return str(value).encode("utf-8") + b"\x00"
    if type_str == "int":
        return struct.pack("<i", int(value))
    if type_str == "bool":
        return struct.pack("<B", 1 if value else 0)
    return struct.pack("<f", float(value))


def decode_scalar(type_str: str, payload: bytes):
    if type_str == "string":
        return payload.split(b"\x00", 1)[0].decode("utf-8")
    if type_str == "int":
        return struct.unpack("<i", payload[:4])[0]
    if type_str == "bool":
        return bool(payload[0])
    return struct.unpack("<f", payload[:4])[0]


def decode_hello(payload: bytes) -> dict:
    fields = struct.unpack("<6I", payload[:24])
    return dict(zip((
        "protocol_version", "abi_version", "capabilities", "max_message",
        "endpoint_kind", "reserved",
    ), fields))


def decode_identity(payload: bytes) -> dict:
    fields = struct.unpack("<3Q4I", payload[:40])
    return dict(zip((
        "graph_hash", "plan_hash", "id_map_hash", "sample_rate",
        "block_size", "id_count", "flags",
    ), fields))


def decode_stats(payload: bytes) -> dict:
    fields = struct.unpack("<4Q", payload[:32])
    return dict(zip(("calls", "errors", "notifications", "dropped"), fields))


def encode_map_request(start: int = 0, limit: int = 0) -> bytes:
    return struct.pack("<2I", start, limit)


def decode_map_page(payload: bytes) -> dict:
    start, total, count, _ = struct.unpack("<4I", payload[:16])
    entries = []
    offset = 16
    for _ in range(count):
        values = struct.unpack("<8I", payload[offset:offset + 32])
        entries.append(dict(zip((
            "id", "kind", "form", "type", "count", "byte_size",
            "module_id", "slot",
        ), values)))
        offset += 32
    return {"start": start, "total": total, "count": count, "entries": entries}
