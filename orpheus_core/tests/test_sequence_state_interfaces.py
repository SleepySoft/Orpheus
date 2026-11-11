"""序列锁存和返回后反馈提交，不依赖块末调度。"""

import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_sequence_source_algorithm(orpheus_test_sequence_source_State* s,
    orpheus_test_sequence_source_IO* io,const OrpheusProcessContext* ctx) {
    const uint32_t sequences[]={1,1,2,2,3,4,UINT32_MAX,0};
    for(unsigned j=0;j<4;j++) {
        unsigned k=s->calls++;
        for(unsigned i=0;i<@N@;i++) io->targets.samples[i]=((int)((i*3+k*5)%17)-8)*.125f;
        memcpy(io->sequence.samples,sequences+k,4);
        io->coefficient.samples[0]=.25f+k*.0625f;
        int rc=orpheus_test_sequence_source_emit_targets(ctx,&io->targets);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_sequence_capture_algorithm(orpheus_test_sequence_capture_State* s,
    orpheus_test_sequence_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t starts[]={@STARTS@},targets[]={@TARGETS@};
    static const int32_t frozen[]={1,1,0,0,1,0,1,0};
    if(s->calls>=8 || ctx->trigger_input!=io->start.input_index || ctx->frame_count!=8 ||
        ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/4*8) ||
        io->start.buffer->frame_count!=1 || io->accepted.buffer->frame_count!=1 ||
        memcmp(io->start.samples,starts+s->calls*@N@,@N@*4) ||
        memcmp(io->accepted.samples,targets+s->calls*@N@,@N@*4) ||
        io->alpha.samples[0]!=.25f || io->freeze.samples[0]!=frozen[s->calls])
        return ORPHEUS_ERR_PROCESSING;
    for(unsigned i=0;i<@N@;i++) {
        volatile float feed=io->accepted.samples[i]*(1.0f-io->alpha.samples[0]);
        volatile float back=io->start.samples[i]*io->alpha.samples[0];
        io->feedback.samples[i]=frozen[s->calls] ? io->start.samples[i] : feed+back;
    }
    s->calls++;s->last=io->feedback.samples[@N@-1];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''


@pytest.mark.parametrize("channels", [1, 20, 128])
def test_sequence_latch_latest_feedback_and_repeated_real_calls(tmp_path, channels):
    current = accepted = None
    previous = 0
    starts, targets, last_values = [], [], []
    for k, sequence in enumerate([1, 1, 2, 2, 3, 4, 0xFFFFFFFF, 0]):
        incoming = [f32(((i * 3 + k * 5) % 17 - 8) * .125) for i in range(channels)]
        if current is None:
            current, accepted = incoming[:], incoming[:]
        if sequence != previous and not sequence & 1:
            current = [max(value, f32(.2)) for value in current]
            accepted = incoming[:]
        previous = sequence
        starts.extend(map(bits, current))
        targets.extend(map(bits, accepted))
        if not sequence & 1:
            current = [f32(f32(value * .75) + f32(start * .25))
                       for start, value in zip(current, accepted)]
        last_values.append(current[-1])

    def array(process=False, count=channels, fmt="f32", direction="input"):
        port = {"direction": direction, "type": "bulk", "sample_format": fmt,
                "channels": count, "block_size": 1}
        if process:
            port["process"] = True
        return port

    source = create_component(tmp_path, "sequence_source", {
        "targets": {**array(direction="output"), "drive_port_no": 1},
        "sequence": array(count=1, fmt="i32", direction="output"),
        "coefficient": array(count=1, direction="output"),
        "calls": scalar("int", 0),
    }, '#include <string.h>\n' + SOURCE.replace("@N@", str(channels)), source=True)
    capture = create_component(tmp_path, "sequence_capture", {
        "start": array(process=True), "accepted": array(),
        "alpha": array(count=1), "freeze": array(count=1, fmt="i32"),
        "feedback": array(direction="output"), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@N@", str(channels))
        .replace("@STARTS@", ",".join(map(str, starts)))
        .replace("@TARGETS@", ",".join(map(str, targets))))
    state = ROOT / "components" / "orpheus" / "builtin" / "sequence_envelope_state" / "component.yaml"
    manifests = [source, state, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=8, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.sequence_source"),
        "state": Node("state", "orpheus.builtin.sequence_envelope_state",
                      params={"channels": channels, "silent": .2}),
        "capture": Node("capture", "orpheus.test.sequence_capture"),
    }, connections=[
        *(Connection(PortRef("source", name), PortRef("state", name))
          for name in ("targets", "sequence", "coefficient")),
        *(Connection(PortRef("state", name), PortRef("capture", name))
          for name in ("start", "accepted", "alpha", "freeze")),
        Connection(PortRef("capture", "feedback"), PortRef("state", "feedback")),
    ]))
    case = compile_delivery_backends(tmp_path, registry, project, manifests, four_entry_harness())
    check_four_entries(case, [[0, 4, pytest.approx(last_values[3], abs=1e-6), 0],
                              [1, 8, pytest.approx(last_values[7], abs=1e-6), 8]])
