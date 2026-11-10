"""双段 EQ 的接口迁移、原生数值、BULK 绑定和双路径字节一致性。"""

import ctypes as C
import math
import uuid

import pytest

from orpheus_core.component_codegen import assert_component_generated_current
from orpheus_core.component_definition import load_component_definition, normalize_component_definition
from test_filters_delay_interfaces import AddSlot, Instance, Registry, ROOT, rbj


PARAMS = {"fc0": 1000.0, "q0": 1.0, "gain_db0": 6.0,
          "fc1": 3000.0, "q1": 1.2, "gain_db1": -3.0}


def register(instance):
    slots = {}

    @AddSlot
    def add(_, pointer):
        slot = pointer.contents
        slots[slot.key.decode()] = (slot.offset, slot.count, slot.size, slot.flags, slot.kind)
        return len(slots)

    registry = Registry(None, add, None)
    assert instance.api.register(instance.state, C.byref(registry)) == 0
    return slots


def test_bank_manifest_is_complete_and_has_no_legacy_header_dependency():
    directory = ROOT / "components" / "orpheus" / "builtin" / "biquad_bank"
    raw = load_component_definition(directory / "component.yaml")
    assert raw["abi_version"] == 7
    assert not ({"ports", "parameters", "bulk_slots", "sources", "headers", "state_type", "deps"} & raw.keys())
    normalized = normalize_component_definition(raw)
    assert {s["id"]: s["count"] for s in normalized["bulk_slots"]} == {"bq0.coefs": 5, "bq1.coefs": 5}
    assert all(s["double_bank"] for s in normalized["bulk_slots"])
    assert_component_generated_current(directory / "component.yaml")
    source = (directory / "user" / "algorithm.c").read_text(encoding="utf-8")
    assert "orpheus_builtin_biquad_bank_emit_out(ctx, &io->out)" in source
    assert "OrpheusComponentDescriptor" not in source and "orpheus_process_next" not in source
    assert not (directory / "src" / "biquad_bank.c").exists()
    assert not (directory / "include" / "orpheus_biquad_bank.h").exists()


def reference(signal, channels):
    result = signal
    for fc, q, gain in [(1000.0, 1.0, 6.0), (3000.0, 1.2, -3.0)]:
        b, a = rbj("peaking", fc, q, gain)
        x1, x2, y1, y2 = ([0.0] * channels for _ in range(4))
        stage = []
        for i, x in enumerate(result):
            c = i % channels
            y = b[0]*x + b[1]*x1[c] + b[2]*x2[c] - a[1]*y1[c] - a[2]*y2[c]
            x2[c], x1[c], y2[c], y1[c] = x1[c], x, y1[c], y
            stage.append(y)
        result = stage
    return result


@pytest.mark.parametrize("form", ["df1", "df2t"])
@pytest.mark.parametrize("channels", [1, 2, 32])
def test_bank_native_cascade_reset_reprepare_and_emit(form, channels):
    instance = Instance("biquad_bank", dict(PARAMS, form=form), channels)
    try:
        assert instance.prepare() == 0
        signal = [0.25 * math.sin(n * 0.17) if c == 0 else 0.0
                  for n in range(512) for c in range(channels)]
        expected = reference(signal, channels)
        assert instance.run(signal) == pytest.approx(expected, abs=2e-5)
        assert instance.api.reset(instance.state) == 0
        assert instance.run(signal) == pytest.approx(expected, abs=2e-5)
        assert instance.prepare() == 0
        assert instance.run(signal) == pytest.approx(expected, abs=2e-5)
        instance.run([0.0] * channels, emit_result=-73)
    finally:
        instance.close()


@pytest.mark.parametrize("form", ["df1", "df2t"])
def test_bank_bulk_slots_drive_real_filter_storage_and_survive_reset(form):
    instance = Instance("biquad_bank", dict(PARAMS, form=form))
    try:
        assert instance.prepare() == 0
        slots = register(instance)
        assert set(slots) == {*PARAMS, "form", "channels", "bq0.coefs", "bq1.coefs"}
        for index in range(2):
            offset, count, size, flags, kind = slots[f"bq{index}.coefs"]
            assert count == 5 and size == C.sizeof(C.c_float)
            assert flags & 16 and kind == 2
            active = (C.c_float * count).from_address(instance.state.value + offset)
            active[:] = [2.0 if index == 0 else 0.25, 0.0, 0.0, 0.0, 0.0]
        signal = [0.25, -0.5, 1.0, 0.0] * 32
        assert instance.api.reset(instance.state) == 0
        assert instance.run(signal) == [x * 0.5 for x in signal]
        assert instance.api.reset(instance.state) == 0
        assert instance.run(signal) == [x * 0.5 for x in signal]
    finally:
        instance.close()


def test_bank_restart_only_parameters_are_validated_without_committing():
    instance = Instance("biquad_bank", dict(PARAMS, form="df2t"))
    try:
        assert instance.prepare() == 0
        for key, setting in [("fc0", 1200.0), ("channels", 2), ("form", "df1")]:
            before = instance.get(key)
            assert instance.set(key, setting) != 0
            assert instance.get(key) == before
        for key, setting in [("q0", 0.0), ("gain_db1", float("nan")), ("fc1", 1)]:
            assert instance.set(key, setting) != 0
    finally:
        instance.close()


@pytest.mark.parametrize("initial", ["2,0,0,0,0", "-0.5,0,0,0,0"])
def test_bank_declared_bulk_initializer_is_used_by_real_algorithm(initial):
    instance = Instance("biquad_bank", {"bq0_coefs": initial, "bq1_coefs": "1,0,0,0,0"})
    try:
        assert instance.prepare() == 0
        scale = float(initial.split(",")[0])
        signal = [0.25, -0.5, 1.0, 0.0] * 32
        assert instance.run(signal) == [x * scale for x in signal]
        assert instance.api.reset(instance.state) == 0
        assert instance.run(signal) == [x * scale for x in signal]
    finally:
        instance.close()


@pytest.mark.parametrize("initial", ["1,0,0,0", "1,0,0,0,0,0", "nan,0,0,0,0", "1,0,0,0,inf", ",0,0,0,0"])
def test_bank_rejects_malformed_coefficient_initializer(initial):
    instance = Instance("biquad_bank", {"bq0_coefs": initial})
    try:
        assert instance.prepare() != 0
    finally:
        instance.close()


@pytest.mark.parametrize("form", ["df1", "df2t"])
def test_bank_dynamic_and_generated_wav_bytes_match(form):
    from editor_client import EditorClient
    from orpheus_core.server.app import create_app

    name = f"bank_interfaces_{uuid.uuid4().hex[:8]}"
    with EditorClient(create_app(ROOT)) as client:
        try:
            response = client.post("/api/projects", json={"name": name, "from_example": "wav_gain_biquad"})
            assert response.status_code == 201, response.text
            doc = client.get(f"/api/projects/{name}").json()
            bank = next(n for n in doc["graph"]["nodes"] if n["id"] == "biquad")
            bank["component"] = "orpheus.builtin.biquad_bank"
            bank["params"] = dict(PARAMS, channels=2, form=form)
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
