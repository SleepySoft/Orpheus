"""真实到达的首声道RMS分箱、曲线探针与直通四入口验证。"""
import math

import pytest

from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_fir_interfaces import CAPTURE
from test_interface_delivery import ROOT, audio, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, check_four_entries, f32, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_sweep_source_algorithm(orpheus_test_sweep_source_State* s,
    orpheus_test_sweep_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<ctx->frame_count;n++) for(unsigned c=0;c<@CHANNELS@;c++) {
            unsigned index=s->calls*@FRAMES@+n;
            io->out.samples[n*@CHANNELS@+c]=c ? (float)(c+1) :
                @QUIET@ ? (s->calls<2 ? .5f : 0.0f) : (float)((int)((index*13)%17)-8)*.03125f;
        }
        int rc=orpheus_test_sweep_source_emit_out(ctx,&io->out);
        if(rc) return rc;
        s->calls++;
    }
    return ORPHEUS_OK;
}
'''

CHECK = r'''
        const float curves[2][512]={@CURVES@};
        const float progress[]={@PROGRESS@},frequency[]={@FREQUENCY@};
        float actual[512],by_id[512];
        OrpheusValue value={ORPHEUS_VALUE_FLOAT,{0}};
#ifdef DYNAMIC
        if(runtime.get_bulk("record","sweep",actual,512) || runtime.get_bulk_id(@ID@,by_id,512) ||
            runtime.get_parameter("record","progress",&value)) return 8;
        if(!runtime.write_bulk("record","sweep",actual,512)) return 9;
#else
        if(orpheus_control_get_bulk("record","sweep",actual,512) ||
            orpheus_control_get_bulk_id(@ID@,by_id,512) ||
            orpheus_control_get_value("record","progress",&value)) return 8;
        if(!orpheus_control_write_bulk("record","sweep",actual,512)) return 9;
#endif
        if(value.type!=ORPHEUS_VALUE_FLOAT || value.value.f32!=progress[tick] ||
            memcmp(actual,by_id,sizeof(actual))) return 10;
        for(unsigned i=0;i<512;i++) if(!isfinite(actual[i]) ||
            fabsf(actual[i]-curves[tick][i])>2e-6f*(1+fabsf(curves[tick][i]))) return 11;
#ifdef DYNAMIC
        if(runtime.get_parameter("record","current_freq",&value)) return 12;
#else
        if(orpheus_control_get_value("record","current_freq",&value)) return 12;
#endif
        if(value.type!=ORPHEUS_VALUE_FLOAT || !isfinite(value.value.f32) ||
            fabsf(value.value.f32-frequency[tick])>2e-6f*(1+fabsf(frequency[tick]))) return 13;
'''


@pytest.mark.parametrize("channels,frames,bins,start,end,logarithmic,duration_frames,quiet", [
    (1, 7, 8, 1, 32, False, 21, False),
    (3, 5, 16, 32, 1, True, 20, False),
    (32, 1, 256, 8, 8, False, 3, False),
    (2, 8, 8, 1, 32, True, 0, True),
])
def test_sweep_record_actual_calls_curve_and_readonly_probe(
    tmp_path, channels, frames, bins, start, end, logarithmic, duration_frames, quiet
):
    rate = 64
    duration = f32(duration_frames / rate)
    curve = [0.0] * 512
    for i in range(bins):
        u = i / (bins - 1)
        curve[i] = f32(start * (end / start) ** u) if logarithmic else f32(start + f32((end - start) * f32(u)))
    count, acc = [0] * bins, [0.0] * bins
    snapshots, progresses, frequencies, expected = [], [], [], []
    elapsed, total, silent, peak, done, current = 0.0, 0, 0, 0.0, False, 0.0
    for call in range(4):
        samples = []
        for n in range(call * frames, (call + 1) * frames):
            samples.extend([float(c + 1) if c else (.5 if call < 2 else 0.0) if quiet
                            else ((n * 13) % 17 - 8) * .03125 for c in range(channels)])
        expected.extend(samples)
        if not done:
            position = min(elapsed, duration) / (duration if duration > 0 else 1)
            frequency = start * (end / start) ** position if logarithmic else start + (end - start) * position
            u = 0 if start == end else math.log(frequency / start) / math.log(end / start) if logarithmic else (frequency - start) / (end - start)
            bin_index = int(min(1, max(0, u)) * (bins - 1) + .5)
            energy = sum(x * x for x in samples[::channels])
            count[bin_index] += frames
            acc[bin_index] = f32(acc[bin_index] + f32(energy))
            curve[256 + bin_index] = f32(math.sqrt(f32(acc[bin_index] / f32(count[bin_index]))))
            rms = f32(math.sqrt(f32(energy / frames)))
            peak = max(peak, rms)
            silent = silent + frames if peak > 0 and rms < f32(peak * f32(.02)) else 0
            elapsed += frames / rate
            total += frames
            done = (duration_frames > 0 and total >= duration_frames) or silent >= rate // 4
            progress = 1.0 if done else f32(sum(value > 0 for value in count) / bins)
            current = f32(frequency)
        snapshots.append(curve[:])
        progresses.append(progress)
        frequencies.append(current)
    source = create_component(tmp_path, "sweep_source", {
        "out": audio("output", channels=channels, drive_port_no=1), "calls": scalar("int", 0),
    }, SOURCE.replace("@CHANNELS@", str(channels)).replace("@FRAMES@", str(frames)).replace("@QUIET@", str(int(quiet))),
        source=True)
    capture = create_component(tmp_path, "fir_capture", {
        "in": audio("input", channels=channels, process=True),
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@EXPECTED@", ",".join(str(bits(v)) for v in expected))
        .replace("@FRAMES@", str(frames)).replace("@COUNT@", str(channels * frames)))
    record = ROOT / "components" / "orpheus" / "builtin" / "sweep_record" / "component.yaml"
    manifests = [source, record, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", sample_rate=rate, block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.sweep_source"),
        "record": Node("record", "orpheus.builtin.sweep_record", params={
            "channels": channels, "start_freq": float(start), "end_freq": float(end),
            "duration_s": duration, "log_scale": logarithmic, "bins": bins}),
        "capture": Node("capture", "orpheus.test.fir_capture"),
    }, connections=[Connection(PortRef("source", "out"), PortRef("record", "in")),
                    Connection(PortRef("record", "out"), PortRef("capture", "in"))]))
    plan = GraphCompiler(registry).compile(project)
    point = next(point for point in plan.id_map if point["node"] == "record" and point["key"] == "sweep")
    assert point["kind"] == "PROBE" and point["form"] == "BULK" and point["count"] == 512
    check = CHECK.replace("@CURVES@", ",".join("{" + ",".join(repr(float(v)) + "f" for v in row) + "}"
                                            for row in snapshots[1::2]))
    check = check.replace("@PROGRESS@", ",".join(repr(float(v)) + "f" for v in progresses[1::2]))
    check = check.replace("@FREQUENCY@", ",".join(repr(float(v)) + "f" for v in frequencies[1::2]))
    check = check.replace("@ID@", str(point["id"]))
    harness = "#include <string.h>\n#include <math.h>\n" + four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})").replace("48000", str(rate))
    harness = harness.replace('        printf("%d', check + '\n        printf("%d')
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    check_four_entries(case, [
        [0, 2, expected[2 * frames * channels - 1], 0], [1, 4, expected[-1], frames],
    ])
