"""MIMO外部误差与逐真实调用轮转泄漏；独立非环形f32参考。"""
import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_fir_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_mimo_source_algorithm(orpheus_test_mimo_source_State* s,
    orpheus_test_mimo_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<ctx->frame_count;n++) {
            unsigned index=s->calls*@FRAMES@+n;
            for(unsigned r=0;r<@REFS@;r++)
                io->ref.samples[n*@REFS@+r]=(float)((int)((index*17+r*31)%103)-51)*.03125f;
            for(unsigned o=0;o<@OUTPUTS@;o++)
                io->error.samples[n*@OUTPUTS@+o]=(float)((int)((index*13+o*7)%29)-14)*.0625f;
        }
        int rc=orpheus_test_mimo_source_emit_error(ctx,&io->error);
        if(rc) return rc;
        rc=orpheus_test_mimo_source_emit_ref(ctx,&io->ref);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("refs,outputs,frames,taps,leak,frozen", [
    (1, 1, 1, 1, .75, False), (2, 3, 7, 5, .5, False),
    (12, 8, 1, 125, 1, False), (3, 2, 3, 4, 0, True),
])
def test_mimo_weight_updates_and_call_scoped_leakage(tmp_path, refs, outputs, frames, taps, leak, frozen):
    count = refs * outputs * taps
    initial = [f32(.0009765625 if count == 12000 else ((k * 3) % 17 - 8) * .03125) for k in range(count)]
    csv = ",".join(repr(value) for value in initial)
    if count == 12000:
        assert len(csv.encode("utf-8")) > 65536
    weights = initial[:]
    steps = [0.0 if frozen else .0625 * (o + 1) for o in range(outputs)]
    expected = []
    for call in range(4):
        if leak < 1:
            o, r = call % outputs, (call // outputs) % refs
            start = (o * refs + r) * taps
            weights[start:start + taps] = [f32(w * leak) for w in weights[start:start + taps]]
        for n in range(call * frames, (call + 1) * frames):
            history = [[0.0 if n < k else (((n - k) * 17 + r * 31) % 103 - 51) * .03125
                        for k in range(taps)] for r in range(refs)]
            norm = f32(.03125)
            for row in history:
                for raw in row:
                    norm = f32(norm + f32(raw * raw))
            for o in range(outputs):
                error = ((n * 13 + o * 7) % 29 - 14) * .0625
                scale = f32(f32(steps[o] * error) / norm)
                value = 0.0
                for r in range(refs):
                    for k, raw in enumerate(history[r]):
                        index = (o * refs + r) * taps + k
                        value = f32(value + f32(weights[index] * raw))
                        weights[index] = f32(weights[index] + f32(scale * raw))
                expected.append(value)
    source = create_component(tmp_path, "mimo_source", {
        "ref": audio("output", channels=refs, drive_port_no=1),
        "error": audio("output", channels=outputs, drive_port_no=2), "calls": scalar("int", 0),
    }, SOURCE.replace("@REFS@", str(refs)).replace("@OUTPUTS@", str(outputs)).replace("@FRAMES@", str(frames)),
        source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=outputs, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(outputs * frames)))
    mimo = ROOT / "components" / "orpheus" / "builtin" / "rnc_mimo_nlms" / "component.yaml"
    registry = Registry()
    manifests = [source, mimo, capture]
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.mimo_source"),
        "mimo": Node("mimo", "orpheus.builtin.rnc_mimo_nlms", params={
            "reference_channels": refs, "output_channels": outputs, "filter_length": taps,
            "step_sizes": ",".join(map(str, steps)), "initial_weights": csv, "leakage": leak, "eps": .03125}),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[Connection(PortRef("source", "error"), PortRef("mimo", "filtered_error")),
                    Connection(PortRef("source", "ref"), PortRef("mimo", "ref")),
                    Connection(PortRef("mimo", "out"), PortRef("capture", "in"))]))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    assert [(edge["source_interface"], edge["target_interface"]) for edge in case[2].call_graph["source"]] == [("ref", "ref")]
    entries = {entry["key"]: entry for entry in case[2].id_map if entry["node"] == "mimo"}
    assert entries["weights"]["count"] == count and entries["step_sizes"]["count"] == outputs
    assert "initial_weights" not in entries
    check_four_entries(case, [
        [0, 2, expected[2 * frames * outputs - 1], 0], [1, 4, expected[-1], frames],
    ])


@pytest.mark.parametrize("bank_mode", ["auto", "off"])
def test_mimo_bulk_commit_updates_real_weights_and_steps(tmp_path, bank_mode):
    from test_dynamic_interface_delivery import run_backend
    manifests = [ROOT / "components" / "orpheus" / "builtin" / name / "component.yaml"
                 for name in ("constant_source", "rnc_mimo_nlms", "null_sink")]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=1, double_bank=bank_mode, graph=Graph(nodes={
        "source": Node("source", "orpheus.builtin.constant_source", params={"channels": 1, "value": 1.0}),
        "mimo": Node("mimo", "orpheus.builtin.rnc_mimo_nlms", params={
            "reference_channels": 1, "output_channels": 1, "filter_length": 1,
            "initial_weights": ".5", "step_sizes": "0", "eps": .25}),
        "capture": Node("capture", "orpheus.builtin.null_sink", params={"channels": 1}),
    }, connections=[Connection(PortRef("source", "out"), PortRef("mimo", "filtered_error")),
                    Connection(PortRef("source", "out"), PortRef("mimo", "ref")),
                    Connection(PortRef("mimo", "out"), PortRef("capture", "in"))]))
    harness = r'''
#include <stdio.h>
#include <string.h>
#ifdef DYNAMIC
#include "orpheus_runtime/runtime.h"
orpheus::Runtime runtime;
#define READ(key,v) runtime.get_bulk("mimo",key,v,1)
#define WRITE(key,v) runtime.write_bulk("mimo",key,v,1)
#else
#include "orpheus_graph.h"
#include "orpheus_control.h"
#define READ(key,v) orpheus_control_get_bulk("mimo",key,v,1)
#define WRITE(key,v) orpheus_control_write_bulk("mimo",key,v,1)
#endif
int main(int argc,char** argv) {
#ifdef DYNAMIC
    if(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]),argv[2])) return 2;
#else
    if(orpheus_generated_init(48000,1)) return 2;
#endif
    float weight=0,step=.25f,replacement=2;
    if(READ("weights",&weight) || weight!=.5f || WRITE("weights",&replacement) ||
        WRITE("step_sizes",&step) || READ("weights",&weight)) return 3;
    float pending=weight;
#ifdef DYNAMIC
    if(runtime.process_task("task_source",1)) return 4;
#else
    if(orpheus_generated_process_task_task_source(1)) return 4;
#endif
    if(READ("weights",&weight) || READ("step_sizes",&step)) return 5;
    printf("%.9g %.9g %.9g\n",pending,weight,step);
    return 0;
}
'''
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    for backend in ("dynamic", "generated_backend"):
        result, rows = run_backend(case, backend)
        assert result.returncode == 0, result.stdout + result.stderr
        expected = [.5 if bank_mode == "auto" else 2, f32(2 + f32(.25 / 1.25)), .25]
        assert len(rows) == 1 and [bits(v) for v in rows[0]] == [bits(v) for v in expected]
