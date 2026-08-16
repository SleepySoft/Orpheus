"""串口设备会话：经 OLINK 串行链路对运行生成代码的远程设备调音调参。

与 RtSession（本地 rt_host 子进程）同构接口，端点层无感：
- CALL → 等 RESPONSE（call_id 匹配，超时重试）；
- NOTIFICATION → 探针缓存（snapshot 与 /rt/status 形状一致）；
- resolve/map 直接由编译期 plan.id_map 本地回答（基址在设备侧，PC 不需要）。

传输由调用方注入（link.serial_port.SerialTransport 或测试替身）：
接口 write(bytes)->int / read(n)->bytes（可短读）/ close()。
"""

from __future__ import annotations

import struct
import threading
import time
from collections import deque
from typing import Any

from orpheus_core.link import message, olink

MAX_LOG_LINES = 500


class SerialSession:
    def __init__(self, transport, id_map: list[dict[str, Any]], *,
                 call_timeout: float = 0.3, call_retries: int = 2,
                 read_chunk: int = 4096):
        self._t = transport
        self._id_map = list(id_map)
        self._by_id = {e["id"]: e for e in self._id_map}
        self._by_node_key = {(e["node"], e["key"]): e for e in self._id_map}
        self.call_timeout = call_timeout
        self.call_retries = call_retries
        self._read_chunk = read_chunk

        self.started_at = time.time()
        self._logs: deque[str] = deque(maxlen=MAX_LOG_LINES)
        self._probes: dict[str, dict[str, Any]] = {}
        self._decoder = olink.Decoder()
        self._pending: dict[int, bytes] = {}      # call_id -> RESPONSE 帧
        self._cond = threading.Condition()
        self._call_lock = threading.Lock()         # 串行化 CALL（设备单线程分发）
        self._call_seq = 0
        self._closed = threading.Event()
        self._link_errors = 0
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._log("串口会话已建立，等待设备帧…")
        self._reader.start()

    # ---------------------------------------------------------------- 基础

    def _log(self, line: str) -> None:
        with self._cond:
            self._logs.append(line)

    @property
    def running(self) -> bool:
        return not self._closed.is_set()

    def close(self) -> None:
        if self._closed.is_set():
            return
        self._closed.set()
        try:
            self._t.close()
        except Exception:
            pass
        with self._cond:
            self._cond.notify_all()

    def stop(self, timeout: float = 3.0) -> None:  # noqa: ARG002 - 与 RtSession 同形
        self.close()
        self._log("串口会话已停止")

    def _read_loop(self) -> None:
        while not self._closed.is_set():
            try:
                data = self._t.read(self._read_chunk)
            except Exception as exc:
                self._link_errors += 1
                self._log(f"串口读错误: {exc}")
                time.sleep(0.2)
                continue
            if not data:
                continue
            for frame in self._decoder.feed(data):
                try:
                    self._dispatch(frame)
                except Exception as exc:  # 坏帧不致命：记录后继续
                    self._log(f"帧分发错误: {exc}")

    def _dispatch(self, frame: bytes) -> None:
        msg = message.parse_frame(frame)
        if msg["type"] == message.RESPONSE:
            with self._cond:
                self._pending[msg["call_id"]] = frame
                self._cond.notify_all()
        elif msg["type"] == message.NOTIFICATION:
            self._handle_notification(msg)
        else:
            self._log(f"忽略非预期帧 type={msg['type']} route=0x{msg['route']:08x}")

    def _handle_notification(self, msg: dict) -> None:
        """设备探针泵上行：route=PROBE id，payload 按条目类型解码。"""
        entry = self._by_id.get(msg["route"])
        if entry is None or entry["kind"] != "PROBE":
            self._log(f"未知 NOTIFICATION route=0x{msg['route']:08x}")
            return
        payload = msg["payload"]
        try:
            if entry["form"] == "BULK" or entry.get("count", 1) > 1:
                n = min(entry.get("count", 1), len(payload) // 4)
                value = list(struct.unpack(f"<{n}f", payload[: n * 4]))
            else:
                value = message.decode_scalar(entry["type"], payload)
        except (struct.error, IndexError):
            self._log(f"探针帧 payload 长度异常: 0x{msg['route']:08x}")
            return
        with self._cond:
            self._probes.setdefault(entry["node"], {})[entry["key"]] = value

    # ---------------------------------------------------------------- CALL

    def _next_call_id(self) -> int:
        self._call_seq = (self._call_seq + 1) & 0xFFFF
        if self._call_seq == 0:
            self._call_seq = 1
        return self._call_seq

    def call(self, route: int, payload: bytes = b"", *,
             call_id: int | None = None, timeout: float | None = None) -> dict:
        """发 CALL 并等 RESPONSE（call_id 匹配）；超时按 call_retries 重发，仍无响应抛错。"""
        if not self.running:
            raise RuntimeError("串口会话已关闭")
        cid = call_id if call_id is not None else self._next_call_id()
        timeout = timeout if timeout is not None else self.call_timeout
        with self._call_lock:
            with self._cond:
                self._pending.pop(cid, None)
            frame = message.make_call(route, cid, payload)
            wire = olink.encode(frame)
            attempts = self.call_retries + 1
            for attempt in range(attempts):
                try:
                    self._t.write(wire)
                except Exception as exc:
                    self._link_errors += 1
                    raise RuntimeError(f"串口写失败: {exc}") from exc
                deadline = time.time() + timeout
                with self._cond:
                    while True:
                        remain = deadline - time.time()
                        if remain <= 0:
                            break
                        self._cond.wait(remain)
                        rsp = self._pending.pop(cid, None)
                        if rsp is not None:
                            out = message.parse_frame(rsp)
                            out["raw"] = rsp
                            return out
                if attempt + 1 < attempts:
                    self._log(f"CALL 0x{route:08x} 超时，重发（{attempt + 1}/{self.call_retries}）")
            self._link_errors += 1
            raise RuntimeError(f"CALL 0x{route:08x} 无响应（{attempts} 次尝试）")

    @staticmethod
    def _check_ok(msg: dict, what: str) -> None:
        if msg["error"]:
            raise RuntimeError(f"{what} 被设备拒绝（ERROR flag）")

    # ---------------------------------------------------------------- 高层操作

    def _entry(self, node: str, key: str) -> dict:
        e = self._by_node_key.get((node, key))
        if e is None:
            raise RuntimeError(f"数据点不存在: {node}.{key}")
        return e

    def set_parameter(self, node: str, param: str, value: Any) -> None:
        e = self._entry(node, param)
        if e["kind"] in ("PROBE", "STATE"):
            raise RuntimeError(f"{node}.{param} 为只读（{e['kind']}）")
        msg = self.call(e["id"], message.encode_scalar(e["type"], value))
        self._check_ok(msg, f"SET {node}.{param}")

    def write_id(self, data_id: int, value: Any) -> None:
        e = self._by_id.get(data_id)
        type_str = e["type"] if e else "float"
        msg = self.call(data_id, message.encode_scalar(type_str, value))
        self._check_ok(msg, f"WRITE 0x{data_id:08x}")

    def read_id(self, data_id: int) -> Any:
        e = self._by_id.get(data_id)
        type_str = e["type"] if e else "float"
        msg = self.call(data_id)
        self._check_ok(msg, f"READ 0x{data_id:08x}")
        return message.decode_scalar(type_str, msg["payload"])

    def write_bulk(self, node: str, key: str, values: list[float]) -> None:
        e = self._entry(node, key)
        self.write_bulk_id(e["id"], values)

    def write_bulk_id(self, data_id: int, values: list[float]) -> None:
        payload = struct.pack(f"<{len(values)}f", *[float(v) for v in values])
        msg = self.call(data_id, payload)
        self._check_ok(msg, f"WRITE_BULK 0x{data_id:08x}")

    def read_bulk(self, node: str | None = None, key: str | None = None,
                  data_id: int | None = None) -> list[float]:
        if data_id is None:
            data_id = self._entry(node, key)["id"]
        msg = self.call(data_id)
        self._check_ok(msg, f"READ_BULK 0x{data_id:08x}")
        payload = msg["payload"]
        return list(struct.unpack(f"<{len(payload) // 4}f", payload[: len(payload) // 4 * 4]))

    def msg(self, msg_hex: str, call_id: int) -> str:
        """裸消息透传（与 RtSession.msg 同形：hex 进 hex 出，NOTIFICATION/错误 → ''）。"""
        frame = bytes.fromhex(msg_hex)
        msg = message.parse_frame(frame)
        if msg["type"] == message.NOTIFICATION:
            self._t.write(olink.encode(frame))
            return ""
        rsp = self.call(msg["route"], msg["payload"], call_id=call_id)
        return rsp["raw"].hex()

    # ---------------------------------------------------------------- 元数据

    def resolve(self, data_id: int) -> dict[str, Any]:
        e = self._by_id.get(data_id)
        if e is None:
            raise RuntimeError(f"未知数据 ID: 0x{data_id:08x}")
        return {
            "id": data_id,
            "kind": e["kind"],
            "form": e["form"],
            "type": e["type"],
            "count": e["count"],
            "node": e["node"],
            "key": e["key"],
            "name": e["name"],
            "base": "device",  # 基址在设备侧；PC 经消息寻址，不需要
        }

    def map_all(self) -> list[dict[str, Any]]:
        return [self.resolve(e["id"]) for e in self._id_map]

    def snapshot(self, max_logs: int = 200) -> dict[str, Any]:
        with self._cond:
            logs = list(self._logs)[-max_logs:]
            probes = {n: dict(p) for n, p in self._probes.items()}
        return {
            "running": self.running,
            "exit_code": None,
            "started_at": self.started_at,
            "logs": logs,
            "probes": probes,
        }
