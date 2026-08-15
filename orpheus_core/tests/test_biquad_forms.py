"""biquad / biquad_bank 滤波结构数值正确性测试（v1.1.0 form 参数）。

验证 df1（传统直接 I 型）与 df2t（直接 II 型转置）两种结构的脉冲响应
都与 RBJ 系数的 float64 参考一致（修复输出历史误作输入历史的递推 bug）。
"""
from __future__ import annotations

import ctypes
import math
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SR = 48000
BLOCK = 128


def _dll(name: str) -> Path:
    for cand in (ROOT / "build" / "components" / f"liborpheus_builtin_{name}.dll",
                 ROOT / "build" / "components" / f"orpheus_builtin_{name}.dll"):
        if cand.exists():
            return cand
    raise FileNotFoundError(name)


# ---------------------------------------------------------------- ctypes ABI

class OrpheusValue(ctypes.Structure):
    class U(ctypes.Union):
        _fields_ = [("f32", ctypes.c_float), ("i32", ctypes.c_int32),
                    ("b", ctypes.c_bool), ("s", ctypes.c_char_p), ("bulk_id", ctypes.c_uint32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", ctypes.c_int), ("u", U)]


class OrpheusBuffer(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("format", ctypes.c_int),
                ("channels", ctypes.c_uint32), ("frame_capacity", ctypes.c_uint32),
                ("frame_count", ctypes.c_uint32), ("interleaved", ctypes.c_bool)]


class OrpheusConfig(ctypes.Structure):
    _fields_ = [("sample_rate", ctypes.c_uint32), ("block_size", ctypes.c_uint32),
                ("channels", ctypes.c_uint32),
                ("param_ids", ctypes.POINTER(ctypes.c_char_p)),
                ("param_values", ctypes.POINTER(OrpheusValue)),
                ("param_count", ctypes.c_uint32),
                ("state_block", ctypes.c_void_p)]


class OrpheusProcessContext(ctypes.Structure):
    _fields_ = [("state", ctypes.c_void_p),
                ("inputs", ctypes.POINTER(ctypes.POINTER(OrpheusBuffer))),
                ("outputs", ctypes.POINTER(ctypes.POINTER(OrpheusBuffer))),
                ("input_count", ctypes.c_uint32), ("output_count", ctypes.c_uint32),
                ("frame_count", ctypes.c_uint32), ("sample_rate", ctypes.c_uint32),
                ("scratch", ctypes.c_void_p), ("scratch_size", ctypes.c_size_t),
                ("timestamp", ctypes.c_double)]


CB = ctypes.CFUNCTYPE


class Iface(ctypes.Structure):
    _fields_ = [("get_descriptor", ctypes.c_void_p),
                ("create", CB(ctypes.c_int, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(OrpheusConfig))),
                ("destroy", CB(ctypes.c_int, ctypes.c_void_p)),
                ("prepare", CB(ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(OrpheusConfig))),
                ("reset", CB(ctypes.c_int, ctypes.c_void_p)),
                ("process", CB(ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(OrpheusProcessContext))),
                ("set_parameter", ctypes.c_void_p), ("get_parameter", ctypes.c_void_p),
                ("get_state_value", ctypes.c_void_p), ("register_slots", ctypes.c_void_p),
                ("hook", ctypes.c_void_p)]


def _load(name: str) -> Iface:
    dll = ctypes.CDLL(str(_dll(name)))
    dll.orpheus_get_interface.restype = ctypes.POINTER(Iface)
    return dll.orpheus_get_interface().contents


def _config(params: dict, channels: int):
    keys = list(params.items())
    ids = (ctypes.c_char_p * len(keys))()
    vals = (OrpheusValue * len(keys))()
    for i, (k, v) in enumerate(keys):
        ids[i] = k.encode()
        if isinstance(v, str):
            vals[i].type = 3  # ORPHEUS_VALUE_STRING
            vals[i].s = v.encode()
        elif isinstance(v, int):
            vals[i].type = 1
            vals[i].i32 = v
        else:
            vals[i].type = 0
            vals[i].f32 = v
    cfg = OrpheusConfig(SR, BLOCK, channels, ids, vals, len(keys), None)
    return cfg, ids, vals


def _run(iface: Iface, params: dict, sig: list[float], channels: int = 1) -> list[float]:
    cfg, ids, vals = _config(params, channels)
    st = ctypes.c_void_p()
    assert iface.create(ctypes.byref(st), ctypes.byref(cfg)) == 0
    assert iface.prepare(st, ctypes.byref(cfg)) == 0
    n_total = len(sig)
    import array
    inarr = array.array("f", sig)
    outarr = array.array("f", [0.0] * BLOCK)
    inbuf = OrpheusBuffer(0, 0, channels, BLOCK, BLOCK, True)
    outbuf = OrpheusBuffer(outarr.buffer_info()[0], 0, channels, BLOCK, BLOCK, True)
    ins = (ctypes.POINTER(OrpheusBuffer) * 1)(ctypes.pointer(inbuf))
    outs = (ctypes.POINTER(OrpheusBuffer) * 1)(ctypes.pointer(outbuf))
    out: list[float] = []
    pos = 0
    while pos < n_total:
        n = min(BLOCK, n_total - pos)
        inbuf.data = inarr.buffer_info()[0] + pos * 4 * channels
        inbuf.frame_count = n
        ctx = OrpheusProcessContext(None, ins, outs, 1, 1, n, SR, None, 0, 0.0)
        assert iface.process(st, ctypes.byref(ctx)) == 0
        out.extend(outarr[:n])
        pos += n
    iface.destroy(st)
    return out


# ------------------------------------------------------- float64 参考（RBJ）

def rbj_lowpass(fc: float, q: float):
    w0 = 2 * math.pi * fc / SR
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2 * q)
    a0 = 1 + alpha
    b = [(1 - cw) / 2 / a0, (1 - cw) / a0, (1 - cw) / 2 / a0]
    a = [1.0, -2 * cw / a0, (1 - alpha) / a0]
    return b, a


def rbj_peaking(fc: float, q: float, gain_db: float):
    A = 10 ** (gain_db / 40)
    w0 = 2 * math.pi * fc / SR
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2 * q)
    a0 = 1 + alpha / A
    b = [(1 + alpha * A) / a0, -2 * cw / a0, (1 - alpha * A) / a0]
    a = [1.0, -2 * cw / a0, (1 - alpha / A) / a0]
    return b, a


def df1_reference(sig: list[float], b: list[float], a: list[float]) -> list[float]:
    x1 = x2 = y1 = y2 = 0.0
    out = []
    for x in sig:
        y = b[0] * x + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        x2, x1 = x1, x
        y2, y1 = y1, y
        out.append(y)
    return out


def _impulse(n: int) -> list[float]:
    return [1.0] + [0.0] * (n - 1)


_N = 512


@pytest.mark.skipif(not (ROOT / "build" / "components").exists(), reason="components not built")
@pytest.mark.parametrize("form", ["df1", "df2t"])
def test_biquad_lowpass_impulse_matches_reference(form):
    iface = _load("biquad")
    out = _run(iface, {"type": "lowpass", "fc": 1000.0, "q": 0.707,
                       "channels": 1, "form": form}, _impulse(_N))
    b, a = rbj_lowpass(1000.0, 0.707)
    ref = df1_reference(_impulse(_N), b, a)
    err = max(abs(o - r) for o, r in zip(out, ref))
    assert err < 2e-5, (form, err)


@pytest.mark.skipif(not (ROOT / "build" / "components").exists(), reason="components not built")
@pytest.mark.parametrize("form", ["df1", "df2t"])
def test_biquad_peaking_impulse_matches_reference(form):
    iface = _load("biquad")
    out = _run(iface, {"type": "peaking", "fc": 2000.0, "q": 2.0, "gain_db": 6.0,
                       "channels": 1, "form": form}, _impulse(_N))
    b, a = rbj_peaking(2000.0, 2.0, 6.0)
    ref = df1_reference(_impulse(_N), b, a)
    err = max(abs(o - r) for o, r in zip(out, ref))
    assert err < 2e-5, (form, err)


@pytest.mark.skipif(not (ROOT / "build" / "components").exists(), reason="components not built")
def test_biquad_lowpass_cutoff_is_minus_3db():
    """稳态正弦在 fc 处应约 -3dB（修复前 fc 处为 -16dB）。"""
    iface = _load("biquad")
    n = 8192
    sig = [0.5 * math.sin(2 * math.pi * 1000.0 * k / SR) for k in range(n)]
    out = _run(iface, {"type": "lowpass", "fc": 1000.0, "q": 0.707, "channels": 1}, sig)
    tail = out[n // 2:]
    amp = max(tail)  # 正弦稳态峰值
    db = 20 * math.log10(amp / 0.5)
    assert abs(db - (-3.0)) < 0.6, db


@pytest.mark.skipif(not (ROOT / "build" / "components").exists(), reason="components not built")
@pytest.mark.parametrize("form", ["df1", "df2t"])
def test_biquad_bank_cascade_matches_reference(form):
    iface = _load("biquad_bank")
    out = _run(iface, {"fc0": 1000.0, "q0": 1.5, "gain_db0": 4.0,
                       "fc1": 4000.0, "q1": 2.0, "gain_db1": -3.0,
                       "channels": 1, "form": form}, _impulse(_N))
    b0, a0 = rbj_peaking(1000.0, 1.5, 4.0)
    b1, a1 = rbj_peaking(4000.0, 2.0, -3.0)
    ref = df1_reference(df1_reference(_impulse(_N), b0, a0), b1, a1)
    err = max(abs(o - r) for o, r in zip(out, ref))
    assert err < 2e-5, (form, err)


@pytest.mark.skipif(not (ROOT / "build" / "components").exists(), reason="components not built")
def test_forms_agree_with_each_other():
    """df1 与 df2t 传递函数相同，脉冲响应应互相吻合（float 舍入量级）。"""
    iface = _load("biquad")
    params = {"type": "highshelf", "fc": 3000.0, "q": 0.9, "gain_db": 5.0, "channels": 1}
    a = _run(iface, {**params, "form": "df1"}, _impulse(_N))
    b = _run(iface, {**params, "form": "df2t"}, _impulse(_N))
    err = max(abs(x - y) for x, y in zip(a, b))
    assert err < 2e-5, err
