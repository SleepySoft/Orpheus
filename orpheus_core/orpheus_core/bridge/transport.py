"""Bridge 字节传输接口；不包含消息、成帧或数据点语义。"""

from __future__ import annotations

from typing import Protocol, runtime_checkable


@runtime_checkable
class ByteTransport(Protocol):
    """可替换字节通道的最小契约。"""

    def read(self, size: int = 4096) -> bytes: ...

    def write(self, data: bytes) -> int: ...

    def close(self) -> None: ...
