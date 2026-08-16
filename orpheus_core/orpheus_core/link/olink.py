"""OLINK：Orpheus 消息上串行字节流的成帧层（Python 实现，与 orpheus_olink.h 语义一致）。

线上帧 = COBS( 消息 || CRC16-CCITT 小端 ) || 0x00 定界。
纯成帧/校验，不含消息语义。与 C 实现做帧级互测（test_olink.py）。
"""

from __future__ import annotations

OLINK_MSG_MAX = 8 + 1023 * 4
CRC_LEN = 2


def crc16_ccitt(data: bytes | bytearray) -> int:
    """CRC16-CCITT-FALSE（poly 0x1021，初值 0xFFFF，不反射）。"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    """标准 COBS：产物保证不含 0x00。"""
    out = bytearray()
    code_pos = len(out)
    out.append(1)  # 占位码字节
    code = 1
    for b in data:
        if b == 0:
            out[code_pos] = code
            code_pos = len(out)
            out.append(1)
            code = 1
        else:
            out.append(b)
            code += 1
            if code == 0xFF:  # run 满 254：收尾且无隐含零
                out[code_pos] = code
                code_pos = len(out)
                out.append(1)
                code = 1
    out[code_pos] = code
    return bytes(out)


def encode(msg: bytes) -> bytes:
    """消息 -> 线上帧（含尾部 0x00 定界）。"""
    crc = crc16_ccitt(msg)
    return cobs_encode(bytes(msg) + crc.to_bytes(2, "little")) + b"\x00"


class Decoder:
    """流式解码器：feed(字节串) -> 本批收到的完整帧列表（CRC 已校验剥除）。

    CRC 错误/超长/垃圾字节都只会丢弃当前帧；0x00 恒为帧界，自动重同步。
    """

    def __init__(self, frame_cap: int = OLINK_MSG_MAX):
        self.frame_cap = frame_cap
        self._buf = bytearray()
        self._code_left = 0
        self._prev_code = 0
        self._overflow = False

    def reset(self) -> None:
        self._buf.clear()
        self._code_left = 0
        self._prev_code = 0
        self._overflow = False

    def _finish(self, out: list[bytes]) -> None:
        n = len(self._buf)
        data = bytes(self._buf)
        self.reset()
        if n < CRC_LEN:
            return
        msg, got = data[:-CRC_LEN], int.from_bytes(data[-CRC_LEN:], "little")
        if not msg:
            return  # 空帧丢弃：§18 消息最小 8 字节，0 与「无帧」无法区分
        if crc16_ccitt(msg) == got:
            out.append(msg)

    def feed(self, data: bytes | bytearray | int) -> list[bytes]:
        out: list[bytes] = []
        if isinstance(data, int):
            data = bytes([data])
        for byte in data:
            if byte == 0x00:
                if self._overflow:
                    self.reset()
                elif self._buf:
                    self._finish(out)
                continue
            if self._code_left > 0:
                self._code_left -= 1
                if len(self._buf) >= self.frame_cap:
                    self._overflow = True
                    continue
                self._buf.append(byte)
                continue
            # 码字节：上一 run 若非 0xFF 则补隐含零（帧起始除外）
            if self._prev_code not in (0xFF, 0):
                if len(self._buf) >= self.frame_cap:
                    self._overflow = True
                    continue
                self._buf.append(0)
            self._prev_code = byte
            self._code_left = byte - 1
        return out
