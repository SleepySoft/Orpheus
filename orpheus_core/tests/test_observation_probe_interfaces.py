"""音频链的波形/频谱观测与真实只读数组槽，覆盖四个执行入口。"""

import numpy as np
import pytest

from frequency_interface_fixtures import parameter
from orpheus_core.compiler import GraphCompiler
from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_spectral_reduce_interfaces import SOURCE
from test_table_interfaces import check_four_entries, four_entry_harness


CAPTURE = r'''
#include "component_state.h"
#include <string.h>
int orpheus_test_observation_capture_algorithm(orpheus_test_observation_capture_State* s,
    orpheus_test_observation_capture_IO* io,const OrpheusProcessContext* ctx) {
    if(s->calls>=4 || ctx->trigger_input || ctx->frame_count!=@FRAMES@ || ctx->valid_frames!=@FRAMES@
        || ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/2*@FRAMES@))
        return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned f=0;f<@FRAMES@;f++) for(int32_t c=0;c<s->channels;c++) {
        float want=((int)((f*17+c*31)%103)-51)*.03125f+s->calls*.125f;
        if(memcmp(&io->in.samples[f*s->channels+c],&want,sizeof(float)))
            return ORPHEUS_ERR_PROCESSING;
    }
    s->calls++;s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''

READBACK = r'''
        float waveform[1023], spectrum[1023], by_id[1023];
#ifdef DYNAMIC
        if(runtime.get_bulk("waveform","waveform",waveform,1023)
            || runtime.get_bulk("spectrum","spectrum",spectrum,1023)
            || !runtime.write_bulk("waveform","waveform",waveform,1023)
            || !runtime.write_bulk("spectrum","spectrum",spectrum,1023)
            || runtime.get_bulk_id(@WAVEFORM_ID@,by_id,1023)
            || memcmp(waveform,by_id,sizeof(waveform))
            || runtime.get_bulk_id(@SPECTRUM_ID@,by_id,1023)
            || !runtime.write_bulk_id(@WAVEFORM_ID@,waveform,1023)
            || !runtime.write_bulk_id(@SPECTRUM_ID@,spectrum,1023)) return 20;
#else
        if(orpheus_control_get_bulk("waveform","waveform",waveform,1023)
            || orpheus_control_get_bulk("spectrum","spectrum",spectrum,1023)
            || !orpheus_control_write_bulk("waveform","waveform",waveform,1023)
            || !orpheus_control_write_bulk("spectrum","spectrum",spectrum,1023)
            || orpheus_control_get_bulk_id(@WAVEFORM_ID@,by_id,1023)
            || memcmp(waveform,by_id,sizeof(waveform))
            || orpheus_control_get_bulk_id(@SPECTRUM_ID@,by_id,1023)
            || !orpheus_control_write_bulk_id(@WAVEFORM_ID@,waveform,1023)
            || !orpheus_control_write_bulk_id(@SPECTRUM_ID@,spectrum,1023)) return 20;
#endif
        if(memcmp(spectrum,by_id,sizeof(spectrum))) return 23;
        if(memcmp(waveform,expected_waveform[tick],sizeof(waveform))) return 21;
        for(unsigned i=0;i<1023;i++)
            if(fabsf(spectrum[i]-expected_spectrum[tick][i])>0.00002f) return 22;
'''


def snapshots(frames, requested):
    size = 64
    while size < requested:
        size *= 2
    ring = [0.0] * 1023
    head = 0
    complete = []
    spectra = np.zeros(1023)
    waveforms, spectrums = [], []
    for call in range(4):
        samples = [((frame * 17) % 103 - 51) * .03125 + call * .125 for frame in range(frames)]
        for sample in samples:
            ring[head] = sample
            head = (head + 1) % 1023
            complete.append(sample)
            if len(complete) == size:
                bins = 2 * np.abs(np.fft.rfft(np.asarray(complete) * np.hanning(size)))[:size // 2] / size
                spectra = np.interp(np.linspace(0, size // 2 - 1, 1023), np.arange(size // 2), bins)
                complete.clear()
        if call % 2:
            waveforms.append(list(ring))
            spectrums.append(spectra.tolist())
    return waveforms, spectrums


def c_rows(rows):
    return "{" + ",".join("{" + ",".join(float(v).hex() + "f" for v in row) + "}" for row in rows) + "}"


@pytest.mark.parametrize("channels,frames,window", [
    (1, 1, 64), (3, 53, 65), (2, 129, 64), (32, 4096, 4096),
])
def test_observation_chain_four_entries(tmp_path, channels, frames, window):
    source = create_component(tmp_path, "observation_source", {
        "out": {"direction": "output", "type": "audio", "sample_format": "f32",
                "channels": "param:channels", "drive_port_no": 1},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
    }, SOURCE.replace("reduce_source", "observation_source"), source=True)
    capture = create_component(tmp_path, "observation_capture", {
        "in": {"direction": "input", "type": "audio", "sample_format": "f32",
               "channels": "param:channels", "process": True},
        "channels": parameter(channels, 32), "calls": scalar("int", 0),
        "last": scalar("float", 0.0), "frame": scalar("int", 0),
    }, CAPTURE.replace("@FRAMES@", str(frames)))
    manifests = [source, *[
        ROOT / "components" / "orpheus" / "builtin" / key / "component.yaml"
        for key in ("probe_waveform", "probe_spectrum")
    ], capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    project = Project(flow_mode="task_sequence", block_size=frames, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.observation_source"),
        "waveform": Node("waveform", "orpheus.builtin.probe_waveform", params={"channels": channels}),
        "spectrum": Node("spectrum", "orpheus.builtin.probe_spectrum", params={
            "channels": channels, "window_size": window,
        }),
        "capture": Node("capture", "orpheus.test.observation_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("waveform", "in")),
        Connection(PortRef("waveform", "out"), PortRef("spectrum", "in")),
        Connection(PortRef("spectrum", "out"), PortRef("capture", "in")),
    ]))
    waveform, spectrum = snapshots(frames, window)
    plan = GraphCompiler(registry).compile(project)
    readback = READBACK
    for node in ("waveform", "spectrum"):
        slot = next(slot for slot in plan.id_map if slot["node"] == node and slot["key"] == node)
        readback = readback.replace(f"@{node.upper()}_ID@", str(slot["id"]) + "U")
    constants = (
        "#include <string.h>\n#include <math.h>\n"
        f"static const float expected_waveform[2][1023]={c_rows(waveform)};\n"
        f"static const float expected_spectrum[2][1023]={c_rows(spectrum)};\n"
    )
    harness = constants + four_entry_harness().replace(",8)", f",{frames})").replace("(8)", f"({frames})")
    harness = harness.replace('printf("%d', readback + '\n        printf("%d')
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    for node in ("waveform", "spectrum"):
        slot = next(slot for slot in case[2].id_map if slot["node"] == node and slot["key"] == node)
        assert (slot["kind"], slot["form"], slot["type"], slot["count"]) == ("PROBE", "BULK", "float", 1023)
    check_four_entries(case, [[0, 2, 0, 0], [1, 4, 0, frames]])
