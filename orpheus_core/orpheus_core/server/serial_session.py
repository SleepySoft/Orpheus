"""串口 Bridge 组合：字节 Transport + OLINK + 半双工核心。"""

from __future__ import annotations

from typing import Any

from orpheus_core.bridge import (
    BridgeCapabilities,
    BridgeSession,
    DuplexMode,
    LogSink,
    OlinkCodec,
)


class SerialSession(BridgeSession):
    """远端设备默认使用半双工、单 outstanding CALL 基线。"""

    def __init__(
        self,
        transport,
        id_map: list[dict[str, Any]],
        *,
        call_timeout: float = 0.3,
        call_retries: int = 2,
        read_chunk: int = 4096,
        log_sink: LogSink | None = None,
        capabilities: BridgeCapabilities | None = None,
        probe_interval: float = 0.0,
    ):
        capabilities = capabilities or BridgeCapabilities(duplex=DuplexMode.HALF)
        super().__init__(
            transport,
            OlinkCodec(),
            id_map,
            capabilities=capabilities,
            call_timeout=call_timeout,
            call_retries=call_retries,
            read_chunk=read_chunk,
            log_sink=log_sink,
            probe_interval=probe_interval,
        )
