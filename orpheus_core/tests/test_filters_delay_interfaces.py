"""五个 ABI7 滤波/延迟包的接口、交付、数值与生命周期回归。"""

import ctypes as C
import math
import uuid
from pathlib import Path

import pytest

from orpheus_core.component_codegen import assert_component_generated_current
from orpheus_core.component_definition import load_component_definition

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("bass", "treble", "midrange", "biquad", "delay")
SR = 48000


class Value(C.Structure):
    class U(C.Union):
        _fields_ = [("f32", C.c_float), ("i32", C.c_int32), ("b", C.c_bool), ("str", C.c_char_p)]

    _anonymous_ = ("value",)
    _fields_ = [("type", C.c_int), ("value", U)]


class Config(C.Structure):
    _fields_ = [
        ("sample_rate", C.c_double), ("block_size", C.c_uint32), ("channels", C.c_uint32),
        ("param_ids", C.POINTER(C.c_char_p)), ("param_values", C.POINTER(Value)),
        ("param_count", C.c_uint32), ("state_block", C.c_void_p),
    ]


class Buffer(C.Structure):
    _fields_ = [
        ("data", C.c_void_p), ("format", C.c_int), ("channels", C.c_uint32),
        ("frame_capacity", C.c_uint32), ("frame_count", C.c_uint32), ("interleaved", C.c_bool),
    ]


class Delivery(C.Structure):
    _fields_ = [("buffer", C.POINTER(Buffer)), ("frame_index", C.c_uint64),
                ("epoch", C.c_uint64), ("timeline_flags", C.c_uint32)]


Emit = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(Delivery))


class Continuation(C.Structure):
    _fields_ = [("user", C.c_void_p), ("call", C.c_void_p), ("count", C.c_uint32), ("emit", Emit)]


class Context(C.Structure):
    _fields_ = [
        ("state", C.c_void_p), ("inputs", C.POINTER(C.POINTER(Buffer))),
        ("outputs", C.POINTER(C.POINTER(Buffer))), ("input_count", C.c_uint32),
        ("output_count", C.c_uint32), ("frame_count", C.c_uint32), ("sample_rate", C.c_double),
        ("scratch", C.c_void_p), ("scratch_size", C.c_size_t), ("timestamp", C.c_double),
        ("frame_index", C.c_uint64), ("epoch", C.c_uint64), ("valid_frames", C.c_uint32),
        ("timeline_flags", C.c_uint32), ("continuation", C.POINTER(Continuation)),
        ("trigger_input", C.c_uint32), ("delivery", C.POINTER(Delivery)),
    ]


class Slot(C.Structure):
    _fields_ = [
        ("kind", C.c_int), ("key", C.c_char_p), ("name", C.c_char_p), ("type", C.c_int),
        ("offset", C.c_size_t), ("size", C.c_size_t), ("count", C.c_uint32),
        ("min_f32", C.c_float), ("max_f32", C.c_float), ("min_i32", C.c_int32),
        ("max_i32", C.c_int32), ("unit", C.c_char_p), ("update_policy", C.c_int),
        ("flags", C.c_uint32),
    ]


AddSlot = C.CFUNCTYPE(C.c_uint64, C.c_void_p, C.POINTER(Slot))


class Registry(C.Structure):
    _fields_ = [("ctx", C.c_void_p), ("add", AddSlot), ("update", C.c_void_p)]


class Descriptor(C.Structure):
    _fields_ = [
        ("id", C.c_char_p), ("version", C.c_char_p), ("abi_version", C.c_uint32),
        ("ports", C.c_void_p), ("port_count", C.c_uint32), ("params", C.c_void_p),
        ("param_count", C.c_uint32), ("state_size", C.c_size_t), ("scratch_size", C.c_size_t),
        ("alignment", C.c_size_t), ("latency_samples", C.c_uint32),
        ("realtime_safe", C.c_bool), ("supports_inplace", C.c_bool),
    ]


class Interface(C.Structure):
    _fields_ = [
        ("descriptor", C.CFUNCTYPE(C.POINTER(Descriptor))),
        ("create", C.CFUNCTYPE(C.c_int, C.POINTER(C.c_void_p), C.POINTER(Config))),
        ("destroy", C.CFUNCTYPE(C.c_int, C.c_void_p)),
        ("prepare", C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Config))),
        ("reset", C.CFUNCTYPE(C.c_int, C.c_void_p)),
        ("process", C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Context))),
        ("set", C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.POINTER(Value))),
        ("get", C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.POINTER(Value))),
        ("state_value", C.c_void_p),
        ("register", C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Registry))),
        ("hook", C.c_void_p),
    ]


def value(v):
    result = Value()
    if isinstance(v, str):
        result.type, result.str = 3, v.encode()
    elif isinstance(v, int):
        result.type, result.i32 = 1, v
    else:
        result.type, result.f32 = 0, v
    return result


class Instance:
    def __init__(self, name, params=None, channels=1, frames=128):
        directory = ROOT / "build" / "components"
        candidates = [directory / f"liborpheus_builtin_{name}.dll", directory / f"orpheus_builtin_{name}.dll",
                      directory / f"liborpheus_builtin_{name}.so"]
        path = next((p for p in candidates if p.exists()), None)
        assert path is not None, f"先构建 orpheus_builtin_{name}"
        self.dll = C.CDLL(str(path))
        self.dll.orpheus_get_interface.restype = C.POINTER(Interface)
        self.api = self.dll.orpheus_get_interface().contents
        descriptor = self.api.descriptor().contents
        assert descriptor.abi_version == 7
        self.memory = C.create_string_buffer(descriptor.state_size + descriptor.alignment)
        address = C.addressof(self.memory)
        address += (-address) % descriptor.alignment
        params = dict(params or {}, channels=channels)
        self.ids = (C.c_char_p * len(params))(*(p.encode() for p in params))
        self.values = (Value * len(params))(*(value(v) for v in params.values()))
        self.config = Config(SR, frames, channels, self.ids, self.values, len(params), address)
        self.state = C.c_void_p()
        self.channels = channels
        assert self.api.create(C.byref(self.state), C.byref(self.config)) == 0

    def prepare(self):
        return self.api.prepare(self.state, C.byref(self.config))

    def get(self, key):
        v = Value()
        assert self.api.get(self.state, key.encode(), C.byref(v)) == 0
        return v.str.decode() if v.type == 3 else v.i32 if v.type == 1 else v.f32

    def set(self, key, v):
        return self.api.set(self.state, key.encode(), C.byref(value(v)))

    def run(self, samples, emit_result=0, inplace=False):
        frames = len(samples) // self.channels
        source = (C.c_float * len(samples))(*samples)
        output = source if inplace else (C.c_float * len(samples))()
        ib = Buffer(C.cast(source, C.c_void_p), 0, self.channels, frames, frames, True)
        ob = Buffer(C.cast(output, C.c_void_p), 0, self.channels, frames, 0, True)
        inputs = (C.POINTER(Buffer) * 1)(C.pointer(ib))
        outputs = (C.POINTER(Buffer) * 1)(C.pointer(ob))
        deliveries = []

        @Emit
        def emitted(_, port, delivery):
            d = delivery.contents
            deliveries.append((port, d.buffer.contents.frame_count, d.frame_index, d.epoch, d.timeline_flags))
            return emit_result

        continuation = Continuation(None, None, 1, emitted)
        ctx = Context(state=self.state, inputs=inputs, outputs=outputs, input_count=1, output_count=1,
                      frame_count=frames, sample_rate=SR, frame_index=314, epoch=9, valid_frames=frames,
                      timeline_flags=1, continuation=C.pointer(continuation), trigger_input=0)
        result = self.api.process(self.state, C.byref(ctx))
        assert result == emit_result
        assert deliveries == [(1, frames, 314, 9, 1)]
        return list(output)

    def close(self):
        assert self.api.destroy(self.state) == 0
        assert self.api.destroy(self.state) == 0


@pytest.mark.parametrize("name", NAMES)
def test_interfaces_are_only_fact_source(name):
    path = ROOT / "components" / "orpheus" / "builtin" / name
    raw = load_component_definition(path / "component.yaml")
    assert raw["abi_version"] == 7
    assert not ({"ports", "parameters", "sources", "headers", "state_type"} & raw.keys())
    assert raw["interfaces"]["in"]["process"]
    assert raw["interfaces"]["out"]["drive_port_no"] == 1
    assert raw["interfaces"]["out"]["drive_divide"] == 1
    assert_component_generated_current(path / "component.yaml")
    source = (path / "user" / "algorithm.c").read_text(encoding="utf-8")
    assert f"orpheus_builtin_{name}_emit_out(ctx, &io->out)" in source
    assert "orpheus_process_next" not in source and "OrpheusComponentDescriptor" not in source
    assert not (path / "src" / f"{name}.c").exists()


@pytest.mark.parametrize("name", NAMES)
def test_parameter_validation_slots_reset_and_emit(name):
    instance = Instance(name, {"ramp_ms": 50.0} if name in NAMES[:3] else {})
    try:
        assert instance.prepare() == 0
        keys = {}

        @AddSlot
        def add(_, slot):
            s = slot.contents
            keys[s.key.decode()] = (s.type, s.offset, s.size, s.count, s.update_policy, s.flags)
            return len(keys)

        registry = Registry(None, add, None)
        assert instance.api.register(instance.state, C.byref(registry)) == 0
        raw = load_component_definition(ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml")
        settings = {k: v for k, v in raw["interfaces"].items() if "direction" not in v}
        assert keys.keys() == settings.keys()
        assert all(meta[3] == 1 and meta[5] & 3 == 3 for meta in keys.values())
        assert instance.get("channels") == 1
        assert instance.set("channels", 0) != 0
        assert instance.set("channels", 32) != 0
        assert instance.get("channels") == 1
        key = "mix" if name == "delay" else "gain_db"
        previous = instance.get(key)
        assert instance.set(key, 999.0) != 0
        assert instance.set(key, float("nan")) != 0
        assert instance.set(key, 1) != 0
        assert instance.get(key) == previous
        signal = [1.0] + [0.0] * 127
        first = instance.run(signal)
        assert instance.api.reset(instance.state) == 0
        assert instance.run(signal) == first
        instance.run(signal, emit_result=-73)
        assert instance.prepare() == 0
        assert instance.run(signal) == first
    finally:
        instance.close()


def tone_reference(name, signal, channels, fc, q, gain_db):
    boost = 10 ** (gain_db / 20) - 1
    w = 2 * math.pi * fc / SR
    pole = math.exp(-w)
    alpha = math.sin(w) / (2 * q)
    b0, a1, a2 = alpha / (1 + alpha), -2 * math.cos(w) / (1 + alpha), (1 - alpha) / (1 + alpha)
    z1, z2, result = [0.0] * channels, [0.0] * channels, []
    for i, x in enumerate(signal):
        c = i % channels
        if name == "midrange":
            filtered = b0 * x + z1[c]
            z1[c], z2[c] = z2[c] - a1 * filtered, -b0 * x - a2 * filtered
        else:
            low = (1 - pole) * x + pole * z1[c]
            z1[c] = low
            filtered = low if name == "bass" else x - low
        result.append(x + boost * filtered)
    return result


@pytest.mark.parametrize("name,fc", [("bass", 200.0), ("treble", 4000.0), ("midrange", 1000.0)])
@pytest.mark.parametrize("channels", [1, 2, 32])
@pytest.mark.parametrize("gain_db", [-12.0, 0.0, 12.0])
def test_tone_numeric_reference(name, fc, channels, gain_db):
    params = {"fc": fc, "gain_db": gain_db, "ramp_ms": 0.0}
    if name == "midrange":
        params["q"] = 1.0
    instance = Instance(name, params, channels)
    try:
        assert instance.prepare() == 0
        signal = [0.25 * math.sin(i * 0.17) if i % channels == 0 else 0.0 for i in range(128 * channels)]
        expected = tone_reference(name, signal, channels, fc, 1.0, gain_db)
        assert instance.run(signal) == pytest.approx(expected, abs=2e-5)
        assert instance.api.reset(instance.state) == 0
        assert instance.run(signal) == pytest.approx(expected, abs=2e-5)
    finally:
        instance.close()


@pytest.mark.parametrize("name", NAMES[:3])
def test_gain_smoothing_readback_and_reset_does_not_snap(name):
    instance = Instance(name, {"gain_db": 0.0, "ramp_ms": 50.0})
    try:
        assert instance.prepare() == 0
        assert instance.set("gain_db", 12.0) == 0
        assert instance.get("gain_db") == 0.0
        instance.run([0.0] * 128)
        coefficient = 1 - math.exp(-1 / (0.05 * SR))
        boost = (10 ** (12 / 20) - 1) * (1 - (1 - coefficient) ** 128)
        assert instance.get("gain_db") == pytest.approx(20 * math.log10(boost + 1), abs=3e-4)
        before = instance.get("gain_db")
        assert instance.api.reset(instance.state) == 0
        assert instance.get("gain_db") == before
    finally:
        instance.close()


@pytest.mark.parametrize("name", NAMES[:3])
def test_create_does_not_prepare_tone(name):
    instance = Instance(name, {"gain_db": 6.0})
    try:
        assert instance.get("gain_db") == 0.0
        assert instance.prepare() == 0
        assert instance.get("gain_db") == pytest.approx(6.0, abs=1e-6)
    finally:
        instance.close()


@pytest.mark.parametrize("key,setting", [("type", "unknown"), ("form", "unknown")])
def test_biquad_declared_string_domain_and_failed_prepare_cleanup(key, setting):
    instance = Instance("biquad", {key: setting})
    try:
        assert instance.prepare() != 0
        assert instance.prepare() != 0
    finally:
        instance.close()


def rbj(kind, fc, q, gain):
    w = 2 * math.pi * fc / SR
    cw, sw = math.cos(w), math.sin(w)
    alpha, A = sw / (2 * q), 10 ** (gain / 40)
    if kind == "lowpass":
        b, a = [(1-cw)/2, 1-cw, (1-cw)/2], [1+alpha, -2*cw, 1-alpha]
    elif kind == "highpass":
        b, a = [(1+cw)/2, -(1+cw), (1+cw)/2], [1+alpha, -2*cw, 1-alpha]
    elif kind == "bandpass":
        b, a = [alpha, 0, -alpha], [1+alpha, -2*cw, 1-alpha]
    elif kind == "notch":
        b, a = [1, -2*cw, 1], [1+alpha, -2*cw, 1-alpha]
    elif kind == "peaking":
        b, a = [1+alpha*A, -2*cw, 1-alpha*A], [1+alpha/A, -2*cw, 1-alpha/A]
    else:
        t = 2 * math.sqrt(A) * alpha
        if kind == "lowshelf":
            b = [A*((A+1)-(A-1)*cw+t), 2*A*((A-1)-(A+1)*cw), A*((A+1)-(A-1)*cw-t)]
            a = [(A+1)+(A-1)*cw+t, -2*((A-1)+(A+1)*cw), (A+1)+(A-1)*cw-t]
        else:
            b = [A*((A+1)+(A-1)*cw+t), -2*A*((A-1)+(A+1)*cw), A*((A+1)+(A-1)*cw-t)]
            a = [(A+1)-(A-1)*cw+t, 2*((A-1)-(A+1)*cw), (A+1)-(A-1)*cw-t]
    return [v/a[0] for v in b], [v/a[0] for v in a]


@pytest.mark.parametrize("kind", ["lowpass", "highpass", "bandpass", "notch", "peaking", "lowshelf", "highshelf"])
@pytest.mark.parametrize("form", ["df1", "df2t"])
@pytest.mark.parametrize("channels", [1, 32])
def test_biquad_all_forms_and_types(kind, form, channels):
    instance = Instance("biquad", {"type": kind, "form": form, "fc": 2000.0, "q": 2.0, "gain_db": 6.0}, channels)
    try:
        assert instance.prepare() == 0
        assert instance.get("type") == kind and instance.get("form") == form
        b, a = rbj(kind, 2000, 2, 6)
        x1 = x2 = y1 = y2 = 0.0
        signal, expected = [1.0] + [0.0] * 511, []
        for x in signal:
            y = b[0]*x + b[1]*x1 + b[2]*x2 - a[1]*y1 - a[2]*y2
            x2, x1, y2, y1 = x1, x, y1, y
            expected.append(y)
        signal = [v if c == 0 else 0.0 for v in signal for c in range(channels)]
        expected = [v if c == 0 else 0.0 for v in expected for c in range(channels)]
        assert instance.run(signal, inplace=True) == pytest.approx(expected, abs=2e-5)
        assert instance.api.reset(instance.state) == 0
        assert instance.run(signal) == pytest.approx(expected, abs=2e-5)
    finally:
        instance.close()


@pytest.mark.parametrize("channels", [1, 2, 32])
@pytest.mark.parametrize("delay_ms", [0.0, 0.0625, 1.0])
def test_delay_ring_wrap_reset_and_reprepare(channels, delay_ms):
    instance = Instance("delay", {"delay_ms": delay_ms, "mix": 1.0}, channels, frames=7)
    try:
        assert instance.prepare() == 0
        frames = 233
        signal = [float((n + c) % 17 - 8) / 8 for n in range(frames) for c in range(channels)]
        delay = int(delay_ms * SR / 1000 + 0.5) * channels
        expected = signal if delay == 0 else ([0.0] * delay + signal)[:len(signal)]
        actual = []
        for start in range(0, frames, 7):
            actual.extend(instance.run(signal[start * channels:min(start + 7, frames) * channels]))
        assert actual == expected
        assert instance.api.reset(instance.state) == 0
        assert instance.run(signal[:7 * channels]) == expected[:7 * channels]
        assert instance.prepare() == 0
        assert instance.run(signal[:7 * channels]) == expected[:7 * channels]
    finally:
        instance.close()


def test_delay_mix_smoothing_is_per_interleaved_sample_and_reset_snaps():
    instance = Instance("delay", {"delay_ms": 100.0, "mix": 0.0}, channels=2)
    try:
        assert instance.prepare() == 0
        assert instance.set("mix", 1.0) == 0
        assert instance.get("mix") == 1.0
        expected = [math.exp(-(i + 1) / (0.005 * SR)) for i in range(256)]
        assert instance.run([1.0] * 256) == pytest.approx(expected, abs=3e-6)
        assert instance.api.reset(instance.state) == 0
        assert instance.run([1.0] * 256) == [0.0] * 256
    finally:
        instance.close()


def test_five_component_chain_generated_matches_dynamic_bytes():
    from editor_client import EditorClient
    from orpheus_core.server.app import create_app

    name = f"filters_delay_{uuid.uuid4().hex[:8]}"
    with EditorClient(create_app(ROOT)) as client:
        try:
            response = client.post("/api/projects", json={"name": name, "from_example": "wav_gain_biquad"})
            assert response.status_code == 201, response.text
            doc = client.get(f"/api/projects/{name}").json()
            nodes = doc["graph"]["nodes"]
            inlet = next(n for n in nodes if n["id"] == "wav_in")
            outlet = next(n for n in nodes if n["id"] == "wav_out")
            settings = {
                "bass": {"gain_db": 6.0, "fc": 200.0, "ramp_ms": 50.0},
                "treble": {"gain_db": -3.0, "fc": 4000.0, "ramp_ms": 50.0},
                "midrange": {"gain_db": 4.0, "fc": 1000.0, "q": 1.0, "ramp_ms": 50.0},
                "biquad": {"type": "peaking", "form": "df1", "fc": 2000.0, "q": 2.0, "gain_db": 6.0},
                "delay": {"delay_ms": 0.0625, "mix": 0.75},
            }
            doc["graph"]["nodes"] = [inlet, *[
                {"id": key, "component": f"orpheus.builtin.{key}", "task": "default",
                 "params": dict(params, channels=2)}
                for key, params in settings.items()
            ], outlet]
            chain = ["wav_in", *settings, "wav_out"]
            doc["graph"]["connections"] = [
                {"from": f"{a}:out", "to": f"{b}:in"} for a, b in zip(chain, chain[1:])
            ]
            response = client.put(f"/api/projects/{name}", json=doc)
            assert response.status_code == 200, response.text
            output = ROOT / "workspace" / name / "outputs" / "test_output.wav"
            response = client.post(f"/api/projects/{name}/run")
            assert response.status_code == 200 and response.json()["status"] == "ok", response.text
            dynamic = output.read_bytes()
            response = client.post(f"/api/projects/{name}/run_generated")
            assert response.status_code == 200 and response.json()["status"] == "ok", response.text
            assert output.read_bytes() == dynamic
        finally:
            client.delete(f"/api/projects/{name}")
