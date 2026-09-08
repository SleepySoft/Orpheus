"""HLOS Transport 测试用 §18 echo Endpoint。"""

from __future__ import annotations

import struct

from orpheus_core.bridge import LengthPrefixCodec, StdioTransport
from orpheus_core.link import message

VALUE_ROUTE = 0x00010001


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
            if payload:
                value = payload[:4]
                response_payload = b""
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


if __name__ == "__main__":
    serve(StdioTransport())
