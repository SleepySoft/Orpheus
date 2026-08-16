"""串口字节传输（协议层 L1）：pyserial 的薄封装。

只提供字节收发与端口枚举，不含任何消息/成帧语义（成帧见 link/olink.py，
消息分发见 Runtime::message / orpheus_control_message）。pyserial 为可选依赖：
未安装时构造/枚举抛出带安装提示的 RuntimeError，不影响本地（stdio 管道）路径。
"""

from __future__ import annotations


def _serial():
    try:
        import serial  # pyserial
        return serial
    except ImportError as exc:
        raise RuntimeError("串口支持需要 pyserial：pip install pyserial") from exc


def list_ports() -> list[dict]:
    """枚举本机串口：[{device, description, hwid}]，无串口返回空列表。"""
    from serial.tools import list_ports as _lp  # type: ignore
    return [
        {"device": p.device, "description": p.description or "", "hwid": p.hwid or ""}
        for p in _lp.comports()
    ]


class SerialTransport:
    """阻塞式串口字节通道。timeout 为读超时（秒），写默认阻塞。"""

    def __init__(self, port: str, baud: int = 921600, timeout: float = 0.1):
        serial = _serial()
        self._port = serial.Serial(port, baudrate=baud, bytesize=8,
                                   parity="N", stopbits=1, timeout=timeout)

    @property
    def in_waiting(self) -> int:
        return self._port.in_waiting

    def read(self, n: int = 4096) -> bytes:
        return self._port.read(n)

    def write(self, data: bytes) -> int:
        return self._port.write(data)

    def reset_input(self) -> None:
        self._port.reset_input_buffer()

    def close(self) -> None:
        self._port.close()

    def __enter__(self) -> "SerialTransport":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
