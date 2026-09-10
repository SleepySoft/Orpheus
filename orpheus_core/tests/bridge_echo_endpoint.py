"""HLOS Transport 测试用 §18 echo Endpoint。"""

from __future__ import annotations

import struct

from orpheus_core.bridge import LengthPrefixCodec, StdioTransport
from orpheus_core.bridge.identity import fnv1a64, wire_map_entries
from orpheus_core.link import message

VALUE_ROUTE = 0x00010001
ID_MAP = [{
    "id": VALUE_ROUTE, "kind": "RTC", "form": "SCALAR", "type": "float",
    "count": 1,
}]
ID_MAP_HASH = fnv1a64(b"".join(
    struct.pack("<8I", *entry) for entry in wire_map_entries(ID_MAP)
))


def serve(transport) -> None:
    codec = LengthPrefixCodec()
    value = struct.pack("<f", 0.0)
    while True:
        data = transport.read(4096)
        if not data:
            return
        for frame in codec.feed(data):
            parsed = message.parse_frame(frame)
            if parsed["type"] != message.CALL:
                continue
            payload = parsed["payload"]
            response_payload = b""
            if parsed["route"] == message.ROUTE_HELLO:
                response_payload = struct.pack(
                    "<6I", message.BRIDGE_PROTOCOL_VERSION, 4,
                    message.CAP_HALF_DUPLEX | message.CAP_MAP | message.CAP_STOP,
                    4100, 2, 0,
                )
            elif parsed["route"] == message.ROUTE_IDENTITY:
                response_payload = struct.pack(
                    "<3Q4I", 0x11, 0x22, ID_MAP_HASH, 48000, 128, 1,
                    message.IDENTITY_WRITABLE,
                )
            elif parsed["route"] == message.ROUTE_STATS:
                response_payload = struct.pack("<4Q", 1, 0, 0, 0)
            elif parsed["route"] == message.ROUTE_MAP:
                entry = wire_map_entries(ID_MAP)[0]
                response_payload = struct.pack("<4I8I", 0, 1, 1, 0, *entry)
            elif parsed["route"] == message.ROUTE_STOP:
                response_payload = b""
            elif payload:
                value = payload[:4]
            else:
                response_payload = value
            response = message.make_frame(
                parsed["route"], parsed["call_id"], message.RESPONSE,
                response_payload,
            )
            wire = codec.encode(response)
            offset = 0
            while offset < len(wire):
                offset += transport.write(wire[offset:])
            if parsed["route"] == message.ROUTE_STOP:
                return


if __name__ == "__main__":
    serve(StdioTransport())
