"""Bridge 成帧接口与 OLINK 实现。"""

from __future__ import annotations

import struct
from typing import Protocol, runtime_checkable

from orpheus_core.link import olink


@runtime_checkable
class FrameCodec(Protocol):
    """消息帧与 Transport 字节流之间的可替换转换。"""

    def encode(self, frame: bytes) -> bytes: ...

    def feed(self, data: bytes) -> list[bytes]: ...


class OlinkCodec:
    """COBS + CRC16 字节流成帧。"""

    def __init__(self, frame_cap: int = olink.OLINK_MSG_MAX):
        self._decoder = olink.Decoder(frame_cap)

    def encode(self, frame: bytes) -> bytes:
        return olink.encode(frame)

    def feed(self, data: bytes) -> list[bytes]:
        return self._decoder.feed(data)


class LengthPrefixCodec:
    """HLOS 字节流成帧：LE uint32 长度 + 完整 §18 消息。"""

    def __init__(self, frame_cap: int = olink.OLINK_MSG_MAX):
        if frame_cap < 8:
            raise ValueError("frame_cap 不能小于消息头长度")
        self.frame_cap = frame_cap
        self._buffer = bytearray()

    def encode(self, frame: bytes) -> bytes:
        if len(frame) < 8 or len(frame) > self.frame_cap:
            raise ValueError("消息帧长度超出 Codec 范围")
        return struct.pack("<I", len(frame)) + frame

    def feed(self, data: bytes) -> list[bytes]:
        self._buffer.extend(data)
        frames: list[bytes] = []
        while len(self._buffer) >= 4:
            size = struct.unpack_from("<I", self._buffer)[0]
            if size < 8 or size > self.frame_cap:
                self._buffer.clear()
                raise ValueError(f"非法长度前缀: {size}")
            if len(self._buffer) < 4 + size:
                break
            frames.append(bytes(self._buffer[4:4 + size]))
            del self._buffer[:4 + size]
        return frames
