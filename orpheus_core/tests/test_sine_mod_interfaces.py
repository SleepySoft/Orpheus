"""AM交错样本相位与周期表逐帧游标的显式交付验收。"""

import math

import pytest

from frequency_interface_fixtures import parameter
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_noise_slew_interfaces import CAPTURE
from test_spectral_reduce_interfaces import SOURCE, f32
from test_table_interfaces import check_four_entries, four_entry_harness


UPDATE = r'''
    for(int tick=0;tick<2;tick++) {
        if(tick==1) {
            const char* ids[]={"freq_hz","depth"};const float values[]={@FREQ@,@DEPTH@};
            for(unsigned i=0;i<2;i++) {
                OrpheusValue value={ORPHEUS_VALUE_FLOAT};value.value.f32=values[i];
#ifdef DYNAMIC
                if(runtime.set_parameter("modulator",ids[i],value)) return 10;
#else
                if(orpheus_control_set_value("modulator",ids[i],value)) return 10;
#endif
            }
        }
'''


def oracle(channels, frames, frequency, depth, updated, table):
    phase = 0.0
    table_index = 0
    tau = f32(6.283185307179586)
    expected = []
    for call in range(4):
        freq, amount = updated if call >= 2 else (frequency, depth)
        increment = f32(f32(tau * f32(freq)) / 48000)
        row = []
        for frame in range(frames):
            for channel in range(channels):
                sample = f32(((frame * 17 + channel * 31) % 103 - 51) * .03125 + call * .125)
                if table is not None:
                    carrier = table[table_index]
                else:
                    carrier = max(0.0, f32(1 + f32(f32(amount) * f32(math.sin(phase)))))
                    phase = f32(phase + increment)
                    if phase >= tau:
                        phase = f32(phase - tau)
                row.append(f32(sample * carrier))
            if table is not None:
                table_index = (table_index + 1) % len(table)
        expected.append(row)
    return "{" + ",".join("{" + ",".join(v.hex() + "f" for v in row) + "}" for row in expected) + "}"


@pytest.mark.parametrize("channels,frames,frequency,depth,updated,length", [
    (1, 1, 7.0, .25, (20.0, .75), 0),
    (3, 17, 20000.0, 1.0, (.01, 0.0), 0),
    (32, 128, .01, 0.0, (20000.0, 1.0), 0),
    (3, 17, 7.0, .25, (20.0, .75), 5),
    (32, 1024, 7.0, .25, (20.0, .75), 1024),
    (1, 7, 7.0, .25, (20.0, .75), 1),
])
def test_sine_mod_four_entries(tmp_path, channels, frames, frequency, depth, updated, length):
    table = ([0.0, -0.0, -1.0, .5, 1.0] if length == 5 else [-1.0] if length == 1
             else [f32((i % 65 - 32) * .03125) for i in range(length)] if length else None)
    source = create_component(tmp_path, "mod_source", {
        "out": {"direction": "output", "type": "audio", "sample_format": "f32",
                "channels": "param:channels", "drive_port_no": 1},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
    }, SOURCE.replace("reduce_source", "mod_source"), source=True)
    capture_source = CAPTURE.replace("slew_capture", "mod_capture")
    capture_source = capture_source.replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames))
    capture_source = capture_source.replace("@EXPECTED@", oracle(channels, frames, frequency, depth, updated, table))
    if table is None:
        capture_source = capture_source.replace("#include <string.h>", "#include <string.h>\n#include <math.h>")
        capture_source = capture_source.replace(
            "if(memcmp(io->in.samples,expected[s->calls],sizeof(expected[0]))) return ORPHEUS_ERR_PROCESSING;",
            f"for(unsigned i=0;i<{channels * frames};i++) {{"
            "float actual=io->in.samples[i],reference=expected[s->calls][i];"
            "if(!isfinite(actual) || fabsf(actual-reference)>2e-6f*fmaxf(1.0f,fabsf(reference)))"
            "return ORPHEUS_ERR_PROCESSING;}")
    capture = create_component(tmp_path, "mod_capture", {
        "in": {"direction": "input", "type": "audio", "sample_format": "f32",
               "channels": "param:channels", "process": True},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, capture_source)
    manifest = ROOT / "components" / "orpheus" / "builtin" / "sine_mod" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.mod_source"),
        "modulator": Node("modulator", "orpheus.builtin.sine_mod", params={
            "channels": channels, "freq_hz": frequency, "depth": depth,
            "modulation_mode": int(table is not None), "table_length": length or 1,
            "carrier_table": ",".join(format(value, ".9g") for value in table) if table is not None else "1",
        }),
        "capture": Node("capture", "orpheus.test.mod_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("modulator", "in")),
        Connection(PortRef("modulator", "out"), PortRef("capture", "in")),
    ]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    update = UPDATE.replace("@FREQ@", f32(updated[0]).hex() + "f").replace("@DEPTH@", f32(updated[1]).hex() + "f")
    harness = harness.replace("for(int tick=0;tick<2;tick++) {", update)
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, frames]])
