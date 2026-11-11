"""窗函数真实交付与同一系数BULK在四个执行入口的整组提交。"""

import pytest

from frequency_interface_fixtures import parameter
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_spectral_reduce_interfaces import SOURCE, f32
from test_table_interfaces import check_four_entries, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <string.h>
static const float weights[@WINDOW@]=@WEIGHTS@;
int orpheus_test_window_capture_algorithm(orpheus_test_window_capture_State* s,
    orpheus_test_window_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(s->calls>=4 || ctx->trigger_input || ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@
        || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/2*@FRAMES@))
        return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned f=0;f<@FRAMES@;f++) for(unsigned c=0;c<@CHANNELS@;c++) {
        unsigned index=@REPEAT@ ? f%@WINDOW@ : f;
        float weight=index>=@WINDOW@ ? 1 : s->calls>=2 ? .5f : weights[index];
        float input=((int)((f*17+c*31)%103)-51)*.03125f+s->calls*.125f;
        float want=input*weight;
        if(memcmp(&io->in.samples[f*@CHANNELS@+c],&want,sizeof(float)))
            return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''

UPDATE = r'''
    float initial[@WINDOW@], before[@WINDOW@], next[@WINDOW@+1]={0}, readback[@WINDOW@];
    static const float expected[@WINDOW@]=@WEIGHTS@;
#ifdef DYNAMIC
#define READ(v,c) runtime.get_bulk("window","coefficients",v,c)
#define WRITE(v,c) runtime.write_bulk("window","coefficients",v,c)
#else
#define READ(v,c) orpheus_control_get_bulk("window","coefficients",v,c)
#define WRITE(v,c) orpheus_control_write_bulk("window","coefficients",v,c)
#endif
    if(READ(initial,@WINDOW@) || memcmp(initial,expected,sizeof(initial))) return 10;
    for(unsigned i=0;i<@WINDOW@;i++) next[i]=.5f;
    for(int tick=0;tick<2;tick++) {
        if(tick==1) {
            if(WRITE(next,@WINDOW@) || !WRITE(next,@WINDOW@+1)
                || READ(before,@WINDOW@) || memcmp(initial,before,sizeof(initial))) return 11;
        }
'''


@pytest.mark.parametrize("channels,frames,size,repeat,values", [
    (1, 1, 2, False, [1.0]),
    (3, 11, 4, False, [.5, -0.0]),
    (3, 11, 4, True, [.5, 1.0, -.5, -1.0]),
    (2, 7, 16, True, []),
    (32, 4096, 4096, False, [.3, -.7, .125, -0.0] * 1024),
])
def test_window_bulk_four_entries(tmp_path, channels, frames, size, repeat, values):
    weights = [f32(v) for v in values] + [0.0] * (size - len(values))
    if not values:
        weights[0] = 1.0
    replacements = {
        "@CHANNELS@": str(channels), "@FRAMES@": str(frames), "@WINDOW@": str(size),
        "@REPEAT@": str(int(repeat)),
        "@WEIGHTS@": "{" + ",".join(v.hex() + "f" for v in weights) + "}",
    }

    def expand(text):
        for key, value in replacements.items():
            text = text.replace(key, value)
        return text

    source = create_component(tmp_path, "window_source", {
        "out": {"direction": "output", "type": "audio", "sample_format": "f32",
                "channels": "param:channels", "drive_port_no": 1},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
    }, SOURCE.replace("reduce_source", "window_source"), source=True)
    capture = create_component(tmp_path, "window_capture", {
        "in": {"direction": "input", "type": "audio", "sample_format": "f32",
               "channels": "param:channels", "process": True},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, expand(CAPTURE))
    manifest = ROOT / "components" / "orpheus" / "builtin" / "window" / "component.yaml"
    manifests = [source, manifest, capture]
    registry = Registry()
    for item in manifests:
        registry.add_manifest(item)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.window_source"),
        "window": Node("window", "orpheus.builtin.window", params={
            "channels": channels, "window_size": size,
            "coefficients": ",".join(map(str, values)), "mode": "repeat" if repeat else "single",
        }),
        "capture": Node("capture", "orpheus.test.window_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("window", "in")),
        Connection(PortRef("window", "out"), PortRef("capture", "in")),
    ]))
    harness = "#include <string.h>\n" + four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace("for(int tick=0;tick<2;tick++) {", expand(UPDATE))
    harness = harness.replace('printf("%d', expand(
        'if(tick==1 && (READ(readback,@WINDOW@) || memcmp(next,readback,sizeof(readback)))) return 12;\n'
        '        printf("%d'))
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    coefficient = next(slot for slot in case[2].id_map if slot["node"] == "window" and slot["key"] == "coefficients")
    assert coefficient["form"] == "BULK" and coefficient["type"] == "float" and coefficient["count"] == size
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, frames]])
