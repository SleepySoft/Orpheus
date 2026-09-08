"""Bridge 成帧接口与 OLINK 实现。"""

from __future__ import annotations

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
