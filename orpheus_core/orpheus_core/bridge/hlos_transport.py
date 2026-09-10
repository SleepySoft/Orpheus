"""HLOS Bridge Transport：stdio、子进程、TCP 与本地命名 Pipe。"""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import threading
from multiprocessing.connection import Client, Connection, Listener
from pathlib import Path
from typing import Any, BinaryIO, Callable, Sequence


class StreamTransport:
    """将独立二进制输入/输出流组合成一个双向 ByteTransport。"""

    def __init__(self, reader: BinaryIO, writer: BinaryIO, *, close_streams: bool = False):
        self._reader = reader
        self._writer = writer
        self._close_streams = close_streams
        self._write_lock = threading.Lock()
        self._closed = False
        self._eof = False

    @property
    def running(self) -> bool:
        return not self._closed and not self._eof

    def read(self, size: int = 4096) -> bytes:
        if self._closed:
            return b""
        read = getattr(self._reader, "read1", None) or self._reader.read
        data = bytes(read(size) or b"")
        if not data:
            self._eof = True
        return data

    def write(self, data: bytes) -> int:
        if self._closed:
            raise RuntimeError("StreamTransport 已关闭")
        with self._write_lock:
            written = self._writer.write(data)
            self._writer.flush()
        return len(data) if written is None else int(written)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self._close_streams:
            for stream in (self._writer, self._reader):
                try:
                    stream.close()
                except OSError:
                    pass


class StdioTransport(StreamTransport):
    """当前进程的 binary stdin/stdout；通常用于 HLOS Endpoint 进程。"""

    def __init__(self, reader: BinaryIO | None = None, writer: BinaryIO | None = None):
        super().__init__(reader or sys.stdin.buffer, writer or sys.stdout.buffer)
        if os.name == "nt" and reader is None and writer is None:
            import msvcrt

            msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)
            msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)


class ProcessTransport(StreamTransport):
    """启动二进制 stdio Endpoint，并以 stdin+stdout 管道对承载 Bridge。"""

    def __init__(self, argv: Sequence[str], *, cwd: Path | str | None = None,
                 env: dict[str, str] | None = None, stderr=None):
        self.process = subprocess.Popen(
            list(argv), cwd=cwd, env=env,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL if stderr is None else stderr,
        )
        if self.process.stdin is None or self.process.stdout is None:
            self.process.kill()
            raise RuntimeError("无法创建 Endpoint stdio 管道")
        super().__init__(self.process.stdout, self.process.stdin, close_streams=True)

    @property
    def running(self) -> bool:
        return super().running and self.process.poll() is None

    def close(self) -> None:
        if self._closed:
            return
        super().close()
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2.0)


class SocketTransport:
    """已连接 stream socket 的 ByteTransport。"""

    def __init__(self, sock: socket.socket):
        self.socket = sock
        self._closed = False
        self._peer_closed = False

    @property
    def running(self) -> bool:
        return not self._closed and not self._peer_closed

    def read(self, size: int = 4096) -> bytes:
        if self._closed:
            return b""
        data = self.socket.recv(size)
        if not data:
            self._peer_closed = True
        return data

    def write(self, data: bytes) -> int:
        if self._closed:
            raise RuntimeError("SocketTransport 已关闭")
        return self.socket.send(data)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        try:
            self.socket.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.socket.close()


class TcpTransport(SocketTransport):
    @classmethod
    def connect(cls, host: str, port: int, *, timeout: float = 5.0) -> "TcpTransport":
        connection = socket.create_connection((host, port), timeout=timeout)
        connection.settimeout(None)
        return cls(connection)


class TcpListener:
    """HLOS TCP Endpoint 监听器；accept 后返回同一 TcpTransport。"""

    def __init__(self, host: str = "127.0.0.1", port: int = 0, *, backlog: int = 1):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind((host, port))
        self.socket.listen(backlog)

    @property
    def address(self) -> tuple[str, int]:
        host, port = self.socket.getsockname()[:2]
        return str(host), int(port)

    def accept(self) -> TcpTransport:
        connection, _ = self.socket.accept()
        return TcpTransport(connection)

    def close(self) -> None:
        self.socket.close()

    def __enter__(self) -> "TcpListener":
        return self

    def __exit__(self, *exc) -> None:
        self.close()


def local_pipe_address(name: str, *, directory: Path | None = None) -> str:
    """生成当前 HLOS 的命名 Pipe 地址。"""
    if not name or any(char in name for char in "/\\"):
        raise ValueError("Pipe 名称必须是非空单段名称")
    if os.name == "nt":
        return rf"\\.\pipe\orpheus-{name}"
    root = directory or Path("/tmp")
    return str(root / f"orpheus-{name}.sock")


class LocalPipeTransport:
    """Windows Named Pipe / Unix Domain Socket 的消息型 ByteTransport。"""

    def __init__(self, connection: Connection):
        self.connection = connection
        self._buffer = bytearray()
        self._closed = False
        self._peer_closed = False

    @property
    def running(self) -> bool:
        return not self._closed and not self._peer_closed

    @classmethod
    def connect(cls, address: str) -> "LocalPipeTransport":
        family = "AF_PIPE" if os.name == "nt" else "AF_UNIX"
        return cls(Client(address, family=family))

    def read(self, size: int = 4096) -> bytes:
        if self._closed:
            return b""
        if not self._buffer:
            try:
                self._buffer.extend(self.connection.recv_bytes())
            except EOFError:
                self._peer_closed = True
                return b""
        data = bytes(self._buffer[:size])
        del self._buffer[:size]
        return data

    def write(self, data: bytes) -> int:
        if self._closed:
            raise RuntimeError("LocalPipeTransport 已关闭")
        self.connection.send_bytes(data)
        return len(data)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self.connection.close()


class LocalPipeListener:
    """跨平台本地命名端点：Windows AF_PIPE，POSIX AF_UNIX。"""

    def __init__(self, address: str):
        self.address = address
        self.family = "AF_PIPE" if os.name == "nt" else "AF_UNIX"
        if self.family == "AF_UNIX":
            Path(address).unlink(missing_ok=True)
        self._listener = Listener(address, family=self.family)

    def accept(self) -> LocalPipeTransport:
        return LocalPipeTransport(self._listener.accept())

    def close(self) -> None:
        self._listener.close()
        if self.family == "AF_UNIX":
            Path(self.address).unlink(missing_ok=True)

    def __enter__(self) -> "LocalPipeListener":
        return self

    def __exit__(self, *exc) -> None:
        self.close()


TransportFactory = Callable[..., Any]


class TransportAdapterRegistry:
    """HLOS Transport 工厂表；用户可注册或替换 Adapter。"""

    def __init__(self):
        self._factories: dict[str, TransportFactory] = {}

    def register(self, name: str, factory: TransportFactory, *, replace: bool = False) -> None:
        if not name:
            raise ValueError("Adapter 名称不能为空")
        if name in self._factories and not replace:
            raise ValueError(f"Transport Adapter 已注册: {name}")
        self._factories[name] = factory

    def create(self, name: str, **config):
        factory = self._factories.get(name)
        if factory is None:
            raise ValueError(f"Transport Adapter 未注册: {name}")
        return factory(**config)

    def names(self) -> tuple[str, ...]:
        return tuple(sorted(self._factories))


def default_hlos_adapters() -> TransportAdapterRegistry:
    registry = TransportAdapterRegistry()
    registry.register("stdio", lambda **config: StdioTransport(**config))
    registry.register("process", lambda **config: ProcessTransport(**config))
    registry.register("tcp", lambda **config: TcpTransport.connect(**config))
    registry.register("pipe", lambda **config: LocalPipeTransport.connect(**config))
    return registry
