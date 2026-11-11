"""数据输入不驱动，峰值事件按真实到达推进并依次交付两个标量出口。"""
import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_tracker_source_algorithm(orpheus_test_tracker_source_State* s,
    orpheus_test_tracker_source_IO* io,const OrpheusProcessContext* ctx) {
    const float rms[]={-10,-50,-51,-20},peak[]={-5,-40,-30,-60};
    const float volume[]={0,2,-7,4},trigger[]={0,.5f,1,1};
    for(unsigned n=0;n<2;n++) {
        if(s->calls>=4) return ORPHEUS_ERR_PROCESSING;
        unsigned call=(unsigned)s->calls++;
        io->rms.samples[0]=rms[call];io->peak.samples[0]=peak[call];
        io->volume.samples[0]=volume[call];io->trigger.samples[0]=trigger[call];
        int rc=orpheus_test_tracker_source_emit_rms(ctx,&io->rms);
        if(rc) return rc;
        rc=orpheus_test_tracker_source_emit_volume(ctx,&io->volume);
        if(rc) return rc;
        rc=orpheus_test_tracker_source_emit_trigger(ctx,&io->trigger);
        if(rc) return rc;
        rc=orpheus_test_tracker_source_emit_peak(ctx,&io->peak);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <math.h>
int orpheus_test_tracker_capture_algorithm(orpheus_test_tracker_capture_State* s,
    orpheus_test_tracker_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const float levels[]={@LEVELS@},envelopes[]={@ENVELOPES@};
    unsigned call=(unsigned)s->calls/2,port=(unsigned)s->calls%2;
    if(call>=4 || ctx->trigger_input!=port || ctx->frame_count!=@FRAMES@ ||
       ctx->valid_frames!=@FRAMES@ || ctx->epoch!=1 ||
       ctx->frame_index!=(uint64_t)(call/2)*@FRAMES@) return ORPHEUS_ERR_PROCESSING;
    const OrpheusBuffer* buffer=port ? io->envelope.buffer : io->level.buffer;
    float actual=port ? io->envelope.samples[0] : io->level.samples[0];
    if(!buffer || buffer->frame_count!=1 || !isfinite(actual) ||
       fabsf(actual-(port ? envelopes[call] : levels[call]))>2e-5f) return ORPHEUS_ERR_PROCESSING;
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("mode,frames", [(0.0, 1), (1.0, 65536), (-1.0, 7)])
def test_peak_is_only_driver_and_each_arrival_advances(tmp_path, mode, frames):
    p = {
        "mode": mode, "hold_calls": .5, "hold_margin_db": 3.0, "slow_decay_db": -.25,
        "fast_decay_db": -1.0, "trigger_threshold": .5, "fast_multiplier": 3.0,
    }
    envelope, count, holding = -100.0, 0, True
    levels, envelopes = [], []
    for rms, peak, volume, trigger in zip(
        [-10.0, -50.0, -51.0, -20.0], [-5.0, -40.0, -30.0, -60.0],
        [0.0, 2.0, -7.0, 4.0], [0.0, .5, 1.0, 1.0],
    ):
        level = 0.0
        if mode >= 0:
            value = peak if mode > 0 else rms
            delta = f32(envelope - value)
            if delta <= 0:
                envelope, count, holding = value, 0, True
            else:
                coefficient = p["slow_decay_db"]
                if delta >= p["hold_margin_db"]:
                    count += 1
                    if count > p["hold_calls"]:
                        holding = False
                else:
                    count = 0
                if not holding:
                    coefficient = p["fast_decay_db"]
                if trigger > p["trigger_threshold"]:
                    coefficient = f32(p["fast_multiplier"] * p["fast_decay_db"])
                envelope = f32(envelope + coefficient)
            envelope = min(0.0, max(-100.0, envelope))
            level = max(0.0, f32(f32(envelope + 100.0) + volume))
        levels.append(level)
        envelopes.append(envelope)

    def control(direction, **kwargs):
        return {"direction": direction, "type": "control", "sample_format": "f32",
                "channels": 1, "block_size": 1, **kwargs}

    source = create_component(tmp_path, "tracker_source", {
        **{name: control("output", drive_port_no=i + 1)
           for i, name in enumerate(("rms", "peak", "volume", "trigger"))},
        "calls": scalar("int", 0),
    }, SOURCE, source=True)
    code = CAPTURE.replace("@LEVELS@", ",".join(map(str, levels))).replace(
        "@ENVELOPES@", ",".join(map(str, envelopes))).replace("@FRAMES@", str(frames))
    capture = create_component(tmp_path, "tracker_capture", {
        "level": control("input", process=True), "envelope": control("input", process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, code)
    tracker = ROOT / "components" / "orpheus" / "builtin" / "level_tracker" / "component.yaml"
    manifests = [source, tracker, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    edges = [Connection(PortRef("source", name), PortRef("tracker", name))
             for name in ("rms", "peak", "volume")]
    edges.append(Connection(PortRef("source", "trigger"), PortRef("tracker", "fast_trigger")))
    edges += [Connection(PortRef("tracker", name), PortRef("capture", name))
              for name in ("level", "envelope")]
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.tracker_source"),
        "tracker": Node("tracker", "orpheus.builtin.level_tracker", params=p),
        "capture": Node("capture", "orpheus.test.tracker_capture"),
    }, connections=edges))
    harness = four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [[0, 4, 0, 0], [1, 8, 0, frames]])
