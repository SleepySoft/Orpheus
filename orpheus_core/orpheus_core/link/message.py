"""§18 消息信封的 Python 助手（与 orpheus_abi.h 位布局一致）。

8 字节头：route_id(LE u32) + bits(LE u32)。
bits: 31..30 type | 29..26 flags | 25..10 call_id | 9..0 payload_words。
"""

from __future__ import annotations

import struct

CALL, RESPONSE, NOTIFICATION = 0, 1, 2
FLAG_ERROR = 0x8  # flags 窗口 bit3（即 bits bit29）
FLAG_FRAGMENT = 0x1
MAX_PAYLOAD_WORDS = 0x3FF

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
CAP_BULK_FRAGMENT = 1 << 7

BULK_READ, BULK_WRITE, BULK_COMMIT, BULK_CANCEL = 1, 2, 3, 4
BULK_BUSY = 3
BULK_FRAGMENT_HEADER = struct.Struct("<5I")
BULK_ERRORS = {
    1: "分片参数或数据点无效",
    2: "传输不存在",
    3: "等待块边界或同数组写事务占用",
    4: "传输元数据或重复片内容冲突",
    5: "写入尚未收齐",
    6: "分片事务数或暂存内存达到上限",
    7: "事务已结束",
    8: "事务已过期",
}

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
    if words > MAX_PAYLOAD_WORDS:
        raise ValueError("单条消息负载超过1023个32位字，请使用BULK分片事务")
    bits = (msg_type << 30) | (flags << 26) | (call_id << 10) | words
    return struct.pack("<II", route, bits) + payload.ljust(words * 4, b"\x00")


def make_call(route: int, call_id: int, payload: bytes = b"") -> bytes:
    return make_frame(route, call_id, CALL, payload)


def parse_frame(frame: bytes) -> dict:
    if len(frame) < 8:
        raise ValueError("消息过短（至少 8 字节头）")
    route, bits = struct.unpack("<II", frame[:8])
    words = bits & MAX_PAYLOAD_WORDS
    if len(frame) != 8 + words * 4:
        raise ValueError("消息长度与头部声明不一致")
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


def encode_bulk(type_str: str, values: list[int | float]) -> bytes:
    """按声明编码32位BULK元素；整数不接受浮点取整或布尔隐式转换。"""
    if not values:
        raise RuntimeError("BULK写入不能为空：空payload在协议中表示读取")
    if type_str == "int":
        if any(type(value) is not int or not -(2**31) <= value < 2**31 for value in values):
            raise RuntimeError("整数BULK元素必须为int32，禁止取整、布尔转换及溢出")
        return struct.pack(f"<{len(values)}i", *values)
    if type_str == "float":
        try:
            return struct.pack(f"<{len(values)}f", *[float(value) for value in values])
        except (TypeError, ValueError, OverflowError, struct.error) as exc:
            raise RuntimeError("浮点BULK元素无法编码为float32") from exc
    raise RuntimeError(f"BULK暂不支持元素类型：{type_str}")


def decode_bulk(type_str: str, payload: bytes) -> list[int | float]:
    if len(payload) % 4:
        raise RuntimeError("BULK响应长度不是32位元素的整数倍")
    if type_str not in ("int", "float"):
        raise RuntimeError(f"BULK暂不支持元素类型：{type_str}")
    element = "i" if type_str == "int" else "f"
    return list(struct.unpack(f"<{len(payload) // 4}{element}", payload))


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
