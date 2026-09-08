"""统一访问桥：交互语义、Codec、Transport 与日志 Sink。"""

from .core import BridgeCapabilities, BridgeSession, DuplexMode
from .codec import FrameCodec, OlinkCodec
from .log_sink import AsyncFileLogSink, LogSink, NullLogSink
from .transport import ByteTransport

__all__ = [
    "AsyncFileLogSink",
    "BridgeCapabilities",
    "BridgeSession",
    "ByteTransport",
    "DuplexMode",
    "FrameCodec",
    "LogSink",
    "NullLogSink",
    "OlinkCodec",
]
