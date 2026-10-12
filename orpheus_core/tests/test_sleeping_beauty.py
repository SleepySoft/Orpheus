"""sleeping_beauty 源码等价 ramp、freeze、64ch 与静音映射测试。"""
from __future__ import annotations

import array
import ctypes
import math
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
DLL = ROOT / "build" / "components" / "liborpheus_builtin_sleeping_beauty.dll"
SR = 48000
BLOCK = 32


class OrpheusValue(ctypes.Structure):
    class U(ctypes.Union):
        _fields_ = [
            ("f32", ctypes.c_float),
            ("i32", ctypes.c_int32),
            ("b", ctypes.c_bool),
            ("s", ctypes.c_char_p),
            ("bulk_id", ctypes.c_uint32),
        ]

    _anonymous_ = ("u",)
    _fields_ = [("type", ctypes.c_int), ("u", U)]


class OrpheusBuffer(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_void_p),
        ("format", ctypes.c_int),
        ("channels", ctypes.c_uint32),
        ("frame_capacity", ctypes.c_uint32),
        ("frame_count", ctypes.c_uint32),
        ("interleaved", ctypes.c_bool),
    ]


class OrpheusConfig(ctypes.Structure):
    _fields_ = [
        ("sample_rate", ctypes.c_uint32),
        ("block_size", ctypes.c_uint32),
        ("channels", ctypes.c_uint32),
        ("param_ids", ctypes.POINTER(ctypes.c_char_p)),
        ("param_values", ctypes.POINTER(OrpheusValue)),
        ("param_count", ctypes.c_uint32),
        ("state_block", ctypes.c_void_p),
    ]


class OrpheusProcessContext(ctypes.Structure):
    _fields_ = [
        ("state", ctypes.c_void_p),
        ("inputs", ctypes.POINTER(ctypes.POINTER(OrpheusBuffer))),
        ("outputs", ctypes.POINTER(ctypes.POINTER(OrpheusBuffer))),
        ("input_count", ctypes.c_uint32),
        ("output_count", ctypes.c_uint32),
        ("frame_count", ctypes.c_uint32),
        ("sample_rate", ctypes.c_uint32),
        ("scratch", ctypes.c_void_p),
        ("scratch_size", ctypes.c_size_t),
        ("timestamp", ctypes.c_double),
        ("frame_index", ctypes.c_uint64),
        ("epoch", ctypes.c_uint64),
        ("valid_frames", ctypes.c_uint32),
        ("timeline_flags", ctypes.c_uint32),
    ]


CREATE = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(OrpheusConfig)
)
STATE_CALL = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)
PREPARE = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(OrpheusConfig)
)
PROCESS = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(OrpheusProcessContext)
)
SET_PARAMETER = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(OrpheusValue)
)


class Interface(ctypes.Structure):
    _fields_ = [
        ("get_descriptor", ctypes.c_void_p),
        ("create", CREATE),
        ("destroy", STATE_CALL),
        ("prepare", PREPARE),
        ("reset", STATE_CALL),
        ("process", PROCESS),
        ("set_parameter", SET_PARAMETER),
        ("get_parameter", ctypes.c_void_p),
        ("get_state_value", ctypes.c_void_p),
        ("register_slots", ctypes.c_void_p),
        ("hook", ctypes.c_void_p),
    ]


def _config(params: dict[str, object], channels: int):
    items = list(params.items())
    ids = (ctypes.c_char_p * len(items))()
    values = (OrpheusValue * len(items))()
    strings: list[bytes] = []
    for index, (key, value) in enumerate(items):
        ids[index] = key.encode()
        if isinstance(value, bool):
            values[index].type = 2
            values[index].b = value
        elif isinstance(value, str):
            values[index].type = 3
            strings.append(value.encode())
            values[index].s = strings[-1]
        elif isinstance(value, int):
            values[index].type = 1
            values[index].i32 = value
        else:
            values[index].type = 0
            values[index].f32 = float(value)
    return OrpheusConfig(SR, BLOCK, channels, ids, values, len(items), None), ids, values, strings


def _load_instance(params: dict[str, object], channels: int):
    dll = ctypes.CDLL(str(DLL))
    dll.orpheus_get_interface.restype = ctypes.POINTER(Interface)
    interface = dll.orpheus_get_interface().contents
    config, ids, values, strings = _config(params, channels)
    state = ctypes.c_void_p()
    assert interface.create(ctypes.byref(state), ctypes.byref(config)) == 0
    assert interface.prepare(state, ctypes.byref(config)) == 0
    return dll, interface, state, (config, ids, values, strings)


def _set(interface: Interface, state, key: str, value: float | bool) -> None:
    parameter = OrpheusValue()
    if isinstance(value, bool):
        parameter.type = 2
        parameter.b = value
    else:
        parameter.type = 0
        parameter.f32 = value
    assert interface.set_parameter(state, key.encode(), ctypes.byref(parameter)) == 0


def _process(interface: Interface, state, samples: list[float], channels: int) -> list[float]:
    frames = len(samples) // channels
    input_data = array.array("f", samples)
    output_data = array.array("f", [0.0] * len(samples))
    input_buffer = OrpheusBuffer(
        input_data.buffer_info()[0], 0, channels, frames, frames, True
    )
    output_buffer = OrpheusBuffer(
        output_data.buffer_info()[0], 0, channels, frames, frames, True
    )
    inputs = (ctypes.POINTER(OrpheusBuffer) * 1)(ctypes.pointer(input_buffer))
    outputs = (ctypes.POINTER(OrpheusBuffer) * 1)(ctypes.pointer(output_buffer))
    context = OrpheusProcessContext(
        None, inputs, outputs, 1, 1, frames, SR, None, 0, 0.0, 0, 1, frames, 1
    )
    assert interface.process(state, ctypes.byref(context)) == 0
    return output_data.tolist()


@pytest.mark.skipif(not DLL.exists(), reason="sleeping_beauty not built")
def test_sleeping_beauty_advances_source_equivalent_per_sample_ramp() -> None:
    _, interface, state, keepalive = _load_instance(
        {"channels": 1, "gain_index": 128.0, "chan_map": "1", "ramp_ms": 30.0},
        1,
    )
    _set(interface, state, "gain_index", 10.0)
    output = _process(interface, state, [1.0] * BLOCK, 1)
    target = 10 ** (-30.0 / 20.0)
    blocks = 45
    coefficient = math.exp(math.log(target) / (BLOCK * blocks))
    expected = [coefficient ** (sample + 1) for sample in range(BLOCK)]
    assert output == pytest.approx(expected, rel=2e-6, abs=2e-7)
    interface.destroy(state)
    assert keepalive


@pytest.mark.skipif(not DLL.exists(), reason="sleeping_beauty not built")
def test_sleeping_beauty_freeze_stops_ramp_progress() -> None:
    _, interface, state, keepalive = _load_instance(
        {"channels": 1, "gain_index": 128.0, "chan_map": "1", "ramp_ms": 30.0},
        1,
    )
    _set(interface, state, "freeze", True)
    _set(interface, state, "gain_index", 10.0)
    assert _process(interface, state, [1.0] * BLOCK, 1) == pytest.approx([1.0] * BLOCK)
    interface.destroy(state)
    assert keepalive


@pytest.mark.skipif(not DLL.exists(), reason="sleeping_beauty not built")
def test_sleeping_beauty_can_preserve_unity_until_first_index_update() -> None:
    _, interface, state, keepalive = _load_instance(
        {
            "channels": 1,
            "gain_index": 10.0,
            "chan_map": "1",
            "apply_index_on_prepare": False,
        },
        1,
    )
    assert _process(interface, state, [1.0] * BLOCK, 1) == pytest.approx([1.0] * BLOCK)
    _set(interface, state, "gain_index", 10.0)
    output = _process(interface, state, [1.0] * BLOCK, 1)
    assert output[0] < 1.0
    interface.destroy(state)
    assert keepalive


@pytest.mark.skipif(not DLL.exists(), reason="sleeping_beauty not built")
def test_sleeping_beauty_applies_same_block_controls_atomically() -> None:
    _, interface, state, keepalive = _load_instance(
        {
            "channels": 1,
            "gain_index": 128.0,
            "chan_map": "1",
            "ramp_ms": 30.0,
            "apply_index_on_prepare": False,
        },
        1,
    )
    _set(interface, state, "gain_index", 10.0)
    _set(interface, state, "ramp_ms", 10.0)
    output = _process(interface, state, [1.0] * BLOCK, 1)
    target = 10 ** (-30.0 / 20.0)
    blocks = 15
    coefficient = math.exp(math.log(target) / (BLOCK * blocks))
    assert output == pytest.approx(
        [coefficient ** (sample + 1) for sample in range(BLOCK)],
        rel=2e-6,
        abs=2e-7,
    )
    interface.destroy(state)
    assert keepalive


@pytest.mark.skipif(not DLL.exists(), reason="sleeping_beauty not built")
def test_sleeping_beauty_supports_54_channels_and_minus_one_mutes() -> None:
    channel_map = ",".join(["-1", *("0" for _ in range(53))])
    _, interface, state, keepalive = _load_instance(
        {"channels": 54, "gain_index": 10.0, "chan_map": channel_map},
        54,
    )
    output = _process(interface, state, [1.0] * 54, 54)
    assert output[0] == 0.0
    assert output[1:] == pytest.approx([1.0] * 53)
    interface.destroy(state)
    assert keepalive


@pytest.mark.skipif(not DLL.exists(), reason="sleeping_beauty not built")
def test_sleeping_beauty_mean_lr_policy_matches_balance_goldens() -> None:
    common = {
        "channels": 10,
        "offset": 128.0,
        "mutes_bass": 1.0,
        "ramp_ms": 0.0,
        "center_mono_mode": "mean_lr",
        "chan_map": "0,1,2,3,0,1,0,0,1,1",
        "table_idx": "0,10,31,52,74,95,116,128,138,159,180,202,223,244,255",
        "table_db": "-30,-27,-14,-7,-5,-3,-1,0,-1,-3,-5,-7,-14,-27,-30",
    }
    cut = 10 ** (-5.0 / 20.0)
    mean = (1.0 + cut) * 0.5
    silent = 5.0118723e-7
    cases = [
        (74.0, [1.0, cut, mean, mean, 1.0, cut, 1.0, 1.0, cut, cut]),
        (0.0, [1.0, silent, silent, silent, 1.0, silent, 1.0, 1.0, silent, silent]),
        (255.0, [silent, 1.0, silent, silent, silent, 1.0, silent, silent, 1.0, 1.0]),
    ]
    for gain_index, expected in cases:
        _, interface, state, keepalive = _load_instance(
            {**common, "gain_index": gain_index}, 10
        )
        output = _process(interface, state, [1.0] * 10, 10)
        assert output == pytest.approx(expected, rel=2e-6, abs=2e-7)
        interface.destroy(state)
        assert keepalive
