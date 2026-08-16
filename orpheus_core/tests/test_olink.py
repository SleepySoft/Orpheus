"""OLINK 成帧层测试：COBS/CRC16 单元测试 + C/Python 双实现帧级互测。

C 侧通过 tests/olink_cli.c（按需用工程已配置的编译器现场编译）驱动。
"""
from __future__ import annotations

import random
import struct
import subprocess
from pathlib import Path

import pytest

from orpheus_core.builder import run_cmake_with_msvc_env
from orpheus_core.link import olink

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build"
CLI_EXE = BUILD / "olink_cli.exe"


def _build_cli() -> bool:
    """用主构建已配置的编译器（MSVC/MinGW）现场编译 olink_cli。
    MSVC 经 vcvars 包装执行；/Fo 尾反斜杠会在 cmd 引号中转义出错，
    故以 obj 目录为 cwd、不传 /Fo。"""
    cache = BUILD / "CMakeCache.txt"
    if not cache.exists():
        return False
    text = cache.read_text(encoding="utf-8", errors="replace")
    msvc = "cl.exe" in text
    obj_dir = BUILD / "olink_obj"
    obj_dir.mkdir(exist_ok=True)
    if msvc:
        args = ["cl", "/nologo", "/utf-8", "/I", str(ROOT / "orpheus_abi" / "include"),
                str(ROOT / "tests" / "olink_cli.c"), str(ROOT / "orpheus_abi" / "src" / "olink.c"),
                "/Fe:" + str(CLI_EXE)]
        r = run_cmake_with_msvc_env(args, obj_dir, BUILD)
    else:
        args = ["gcc", "-O1", "-std=c11", "-I", str(ROOT / "orpheus_abi" / "include"),
                str(ROOT / "tests" / "olink_cli.c"), str(ROOT / "orpheus_abi" / "src" / "olink.c"),
                "-o", str(CLI_EXE)]
        r = run_cmake_with_msvc_env(args, ROOT, BUILD)
    if r.returncode != 0:
        print("olink_cli build failed:\n", (r.stdout or "")[-1500:], (r.stderr or "")[-1500:])
    return r.returncode == 0


@pytest.fixture(scope="module")
def cli():
    stale = (not CLI_EXE.exists()
             or any(CLI_EXE.stat().st_mtime < s.stat().st_mtime
                    for s in (ROOT / "tests" / "olink_cli.c",
                              ROOT / "orpheus_abi" / "src" / "olink.c",
                              ROOT / "orpheus_abi" / "include" / "orpheus_olink.h")))
    if stale and not _build_cli():
        pytest.skip("olink_cli 不可用（无构建环境）")
    proc = subprocess.Popen([str(CLI_EXE)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            text=True, encoding="ascii")
    yield proc
    proc.stdin.write("quit\n")
    proc.stdin.flush()
    proc.wait(timeout=5)


def cli_roundtrip(proc, cmd: str, payload: bytes) -> list[str]:
    proc.stdin.write(f"{cmd} {payload.hex()}\n")
    proc.stdin.flush()
    lines = []
    while True:
        line = proc.stdout.readline().strip()
        if line == "":
            raise RuntimeError("olink_cli 意外退出（EOF）")
        if cmd == "crc":
            return [line]
        lines.append(line)
        if cmd == "enc" or line in ("BAD", "ERR"):
            return lines
        if line.startswith("F "):
            return lines  # dec: 一帧一行；本测试每行输入至多一帧


# ------------------------------------------------------------------ 数据样例

def make_msg(route: int, call_id: int, payload: bytes = b"") -> bytes:
    """§18 信封（与 test_message_protocol 同构）。"""
    words = (len(payload) + 3) // 4
    bits = (0 << 30) | (call_id << 10) | words
    return struct.pack("<II", route, bits) + payload.ljust(words * 4, b"\x00")


def samples() -> list[bytes]:
    rng = random.Random(20260815)
    cases = [
        b"\x00",                              # 单零
        b"\x00" * 4,                          # 全零
        bytes(range(1, 255)),                 # 无零，254 字节（满 run）
        bytes(range(1, 255)) + b"\x01",       # 255 字节（跨 run 边界）
        bytes(300),                           # 300 个零
        make_msg(0x00040000, 0x1234, struct.pack("<f", -6.0)),   # 真实控制消息
        make_msg(0x10050008, 0xAA, struct.pack("<5f", 1, 2, 3, 4, 5)),  # bulk 写
        bytes(rng.randrange(256) for _ in range(4096)),          # 4KB 随机（最大帧）
    ]
    return cases


# ------------------------------------------------------------------ Python 单测

def test_crc16_known_vector():
    assert olink.crc16_ccitt(b"123456789") == 0x29B1


def test_cobs_no_zero_byte():
    for s in samples():
        assert 0 not in olink.cobs_encode(s)


def test_python_roundtrip():
    for s in samples():
        wire = olink.encode(s)
        assert wire.endswith(b"\x00") and 0 not in wire[:-1]
        dec = olink.Decoder()
        out = dec.feed(wire)
        assert out == [s], (len(s), len(out))


def test_python_streaming_byte_by_byte():
    dec = olink.Decoder()
    msgs = samples()
    wire = b"".join(olink.encode(m) for m in msgs)
    got = []
    for b in wire:
        got.extend(dec.feed(b))
    assert got == msgs


def test_python_corruption_drop_and_resync():
    """改一个字节 -> CRC 错丢帧；随后好帧自动重同步。"""
    good = olink.encode(b"hello frame")
    bad = bytearray(olink.encode(b"broken frame"))
    bad[3] ^= 0xFF
    if bad[3] == 0:
        bad[3] = 0x55
    dec = olink.Decoder()
    out = dec.feed(bytes(bad) + good)
    assert out == [b"hello frame"]


def test_python_empty_frame_dropped_then_resync():
    """空消息帧（仅 CRC）被丢弃且不影响后续帧。"""
    dec = olink.Decoder()
    wire = olink.encode(b"") + olink.encode(b"real")
    assert dec.feed(wire) == [b"real"]


def test_python_garbage_resync():
    """垃圾字节 + 半个帧 + 两个完整帧：垃圾形成的伪 run 最多吞掉其后一帧，之后恢复。"""
    dec = olink.Decoder()
    wire = (b"\x12\x34\x00garbage" + olink.encode(b"abc")[:5]
            + olink.encode(b"eaten") + olink.encode(b"final"))
    # 'g'=0x67 被当成码字节形成 102 字节伪 run，吞掉 encode(b"eaten") 整帧（以其 0x00 收尾），
    # 下一帧起自动恢复——这正是 COBS 的再同步语义。
    assert dec.feed(wire) == [b"final"]


# ------------------------------------------------------------------ C/Python 互测

def test_cross_crc_agree(cli):
    payload = b"123456789"
    [line] = cli_roundtrip(cli, "crc", payload)
    assert line == f"{olink.crc16_ccitt(payload):04x}"


def test_cross_python_enc_c_dec(cli):
    for s in samples():
        wire = olink.encode(s)
        [line] = cli_roundtrip(cli, "dec", wire)
        assert line == f"F {s.hex()}", (len(s), line[:80])


def test_cross_c_enc_python_dec(cli):
    dec = olink.Decoder()
    for s in samples():
        [line] = cli_roundtrip(cli, "enc", s)
        wire = bytes.fromhex(line)
        assert 0 not in wire[:-1]
        assert dec.feed(wire) == [s], (len(s), line[:80])


def test_cross_c_dec_drops_corrupt(cli):
    bad = bytearray(olink.encode(b"corrupt me"))
    bad[2] ^= 0x01
    if bad[2] == 0:
        bad[2] = 0x42
    [line] = cli_roundtrip(cli, "dec", bytes(bad))
    assert line == "BAD"
