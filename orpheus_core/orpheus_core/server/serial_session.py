"""串口 Bridge 组合：字节 Transport + OLINK + 半双工核心。"""

from __future__ import annotations

import threading
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
        self._probe_interval = max(0.0, probe_interval)
        self._probe_stop = threading.Event()
        super().__init__(
            transport,
            OlinkCodec(),
            id_map,
            capabilities=capabilities,
            call_timeout=call_timeout,
            call_retries=call_retries,
            read_chunk=read_chunk,
            log_sink=log_sink,
        )
        self._probe_thread: threading.Thread | None = None
        if self._probe_interval > 0 and not capabilities.unsolicited:
            self._probe_thread = threading.Thread(target=self._poll_loop, daemon=True)
            self._probe_thread.start()

    def _poll_loop(self) -> None:
        while not self._probe_stop.wait(self._probe_interval):
            if not self.running:
                return
            self.poll_observations()

    def close(self) -> None:
        self._probe_stop.set()
        super().close()
        if self._probe_thread is not None:
            self._probe_thread.join(timeout=max(self.call_timeout * 2, 0.2))
