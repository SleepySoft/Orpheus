"""统一访问桥：交互语义、Codec、Transport 与日志 Sink。"""

from .core import BridgeCapabilities, BridgeSession, DuplexMode
from .codec import FrameCodec, LengthPrefixCodec, OlinkCodec
from .hlos_transport import (
    LocalPipeListener,
    LocalPipeTransport,
    ProcessTransport,
    SocketTransport,
    StdioTransport,
    StreamTransport,
    TcpListener,
    TcpTransport,
    TransportAdapterRegistry,
    default_hlos_adapters,
    local_pipe_address,
)
from .log_sink import AsyncFileLogSink, LogSink, NullLogSink
from .transport import ByteTransport

__all__ = [
    "AsyncFileLogSink",
    "BridgeCapabilities",
    "BridgeSession",
    "ByteTransport",
    "DuplexMode",
    "FrameCodec",
    "LengthPrefixCodec",
    "LocalPipeListener",
    "LocalPipeTransport",
    "LogSink",
    "NullLogSink",
    "OlinkCodec",
    "ProcessTransport",
    "SocketTransport",
    "StdioTransport",
    "StreamTransport",
    "TcpListener",
    "TcpTransport",
    "TransportAdapterRegistry",
    "default_hlos_adapters",
    "local_pipe_address",
]
