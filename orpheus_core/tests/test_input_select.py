"""input_select 基础路由与运行期 override 表测试。"""
from __future__ import annotations

import array
import ctypes
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
DLL = ROOT / "build" / "components" / "liborpheus_builtin_input_select.dll"


class Value(ctypes.Structure):
    class Union(ctypes.Union):
        _fields_ = [("f32", ctypes.c_float), ("i32", ctypes.c_int32), ("b", ctypes.c_bool), ("s", ctypes.c_char_p)]
    _anonymous_ = ("value",)
    _fields_ = [("type", ctypes.c_int), ("value", Union)]


class Buffer(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("format", ctypes.c_int), ("channels", ctypes.c_uint32),
                ("frame_capacity", ctypes.c_uint32), ("frame_count", ctypes.c_uint32), ("interleaved", ctypes.c_bool)]


class Config(ctypes.Structure):
    _fields_ = [("sample_rate", ctypes.c_uint32), ("block_size", ctypes.c_uint32), ("channels", ctypes.c_uint32),
                ("param_ids", ctypes.POINTER(ctypes.c_char_p)), ("param_values", ctypes.POINTER(Value)),
                ("param_count", ctypes.c_uint32), ("state_block", ctypes.c_void_p)]


class Context(ctypes.Structure):
    _fields_ = [("state", ctypes.c_void_p), ("inputs", ctypes.POINTER(ctypes.POINTER(Buffer))),
                ("outputs", ctypes.POINTER(ctypes.POINTER(Buffer))), ("input_count", ctypes.c_uint32),
                ("output_count", ctypes.c_uint32), ("frame_count", ctypes.c_uint32),
                ("sample_rate", ctypes.c_uint32), ("scratch", ctypes.c_void_p), ("scratch_size", ctypes.c_size_t),
                ("timestamp", ctypes.c_double), ("frame_index", ctypes.c_uint64), ("epoch", ctypes.c_uint64),
                ("valid_frames", ctypes.c_uint32), ("timeline_flags", ctypes.c_uint32)]


CREATE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(Config))
STATE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)
PREPARE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(Config))
PROCESS = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(Context))
SET = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(Value))


class Interface(ctypes.Structure):
    _fields_ = [("get_descriptor", ctypes.c_void_p), ("create", CREATE), ("destroy", STATE),
                ("prepare", PREPARE), ("reset", STATE), ("process", PROCESS),
                ("set_parameter", SET), ("get_parameter", ctypes.c_void_p),
                ("get_state_value", ctypes.c_void_p), ("register_slots", ctypes.c_void_p), ("hook", ctypes.c_void_p)]


def _instance():
    dll = ctypes.CDLL(str(DLL))
    dll.orpheus_get_interface.restype = ctypes.POINTER(Interface)
    interface = dll.orpheus_get_interface().contents
    names = (ctypes.c_char_p * 4)(b"channels_in", b"channels_out", b"select", b"override_select")
    values = (Value * 4)()
    values[0].type, values[0].i32 = 1, 4
    values[1].type, values[1].i32 = 1, 4
    values[2].type, values[2].s = 3, b"1,2,3,4"
    values[3].type, values[3].s = 3, b"0,0,0,0"
    config = Config(48000, 1, 4, names, values, 4, None)
    state = ctypes.c_void_p()
    assert interface.create(ctypes.byref(state), ctypes.byref(config)) == 0
    assert interface.prepare(state, ctypes.byref(config)) == 0
    return dll, interface, state, (names, values, config)


def _process(interface, state):
    source = array.array("f", [1.0, 2.0, 3.0, 4.0])
    target = array.array("f", [0.0] * 4)
    input_buffer = Buffer(source.buffer_info()[0], 0, 4, 1, 1, True)
    output_buffer = Buffer(target.buffer_info()[0], 0, 4, 1, 1, True)
    inputs = (ctypes.POINTER(Buffer) * 1)(ctypes.pointer(input_buffer))
    outputs = (ctypes.POINTER(Buffer) * 1)(ctypes.pointer(output_buffer))
    context = Context(None, inputs, outputs, 1, 1, 1, 48000, None, 0, 0.0, 0, 1, 1, 1)
    assert interface.process(state, ctypes.byref(context)) == 0
    return target.tolist()


@pytest.mark.skipif(not DLL.exists(), reason="input_select not built")
def test_override_zero_keeps_base_and_invalid_mutes() -> None:
    _, interface, state, keepalive = _instance()
    assert _process(interface, state) == [1.0, 2.0, 3.0, 4.0]
    override = Value()
    override.type = 3
    override.s = b"0,4,99,-1"
    assert interface.set_parameter(state, b"override_select", ctypes.byref(override)) == 0
    assert _process(interface, state) == [1.0, 4.0, 0.0, 0.0]
    interface.destroy(state)
    assert keepalive
