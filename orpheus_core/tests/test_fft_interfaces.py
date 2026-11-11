"""显式FFT载荷、两出口实际轨迹与独立频谱预期。"""

import numpy as np
import pytest

from orpheus_core.project import Connection, Graph, Node, PortRef, Project
from orpheus_core.registry import Registry
from test_dynamic_interface_delivery import run_backend
from test_interface_delivery import ROOT, compile_delivery_backends, create_component, scalar
from test_table_interfaces import bits, four_entry_harness


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_fft_source_algorithm(orpheus_test_fft_source_State* s,
    orpheus_test_fft_source_IO* io,const OrpheusProcessContext* ctx) {
    for(unsigned repeat=0;repeat<2;repeat++) {
        unsigned call=s->calls++;
        for(unsigned i=0;i<@COUNT@;i++) io->out.samples[i]=((int)((i*29+call*7)%257)-128)*.0078125f;
        int rc=orpheus_test_fft_source_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    return ORPHEUS_OK;
}
'''

CAPTURE = r'''
#include "component_state.h"
#include <math.h>
#include <string.h>
int orpheus_test_fft_capture_algorithm(orpheus_test_fft_capture_State* s,
    orpheus_test_fft_capture_IO* io,const OrpheusProcessContext* ctx) {
    static const uint32_t modes[4][@COUNT@]={@MODES@},spectra[4][@COUNT@]={@SPECTRA@};
    unsigned port=s->calls%3,call=s->calls/3;
    if(s->calls>=12 || ctx->trigger_input!=port || ctx->frame_count!=7 || ctx->valid_frames!=7 ||
        ctx->epoch!=1 || ctx->frame_index!=(uint64_t)(s->calls/6*7))
        return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* inputs[]={io->mode.buffer,io->spectrum.buffer,io->restored.buffer};
    const float* samples=inputs[port]->data;
    if(inputs[port]->frame_count!=@FRAMES@) return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned i=0;i<@COUNT@;i++) {
        float expected;
        if(port==2) expected=((int)((i*29+call*7)%257)-128)*.0078125f;
        else memcpy(&expected,(port ? spectra[call] : modes[call])+i,4);
        float tolerance=port==2 ? .000004f : @TOLERANCE@+fabsf(expected)*.000006f;
        if(!isfinite(samples[i]) || fabsf(samples[i]-expected)>tolerance)
            return ORPHEUS_ERR_PROCESSING;
        s->actual_values[(s->calls%6)*@COUNT@+i]=samples[i];
    }
    s->calls++;s->last=samples[@COUNT@-1];s->frame=(int32_t)ctx->frame_index;
    return ORPHEUS_OK;
}
'''

SNAPSHOT = r'''
        float actual[@SAMPLES@];
#ifdef DYNAMIC
        if(runtime.get_bulk("capture","actual_values",actual,@SAMPLES@)) return 10;
#else
        if(orpheus_control_get_bulk("capture","actual_values",actual,@SAMPLES@)) return 10;
#endif
        for(unsigned i=0;i<@SAMPLES@;i++) {
            uint32_t value;memcpy(&value,actual+i,4);printf("%u ",value);
        }
        printf("\n");
'''


def rows(values):
    return ",".join("{" + ",".join(str(bits(float(v))) for v in row) + "}" for row in values)


@pytest.mark.parametrize("size,batches,channels,mode", [
    (4, 3, 3, "halfcomplex"), (32, 2, 20, "magnitude"),
    (256, 3, 2, "power"), (1024, 1, 1, "halfcomplex"), (4, 64, 32, "power"),
])
def test_fft_named_spectrum_and_roundtrip_four_entries(tmp_path, size, batches, channels, mode):
    frames, count = size * batches, size * batches * channels
    inputs = np.asarray([[((i * 29 + call * 7) % 257 - 128) * .0078125
                          for i in range(count)] for call in range(4)], dtype=np.float32)
    transformed = np.fft.fft(inputs.reshape(4, batches, size, channels), axis=2)
    spectra = transformed.real.copy()
    for k in range(1, size // 2):
        spectra[:, :, size - k, :] = transformed[:, :, k, :].imag
    outputs = {"halfcomplex": spectra, "magnitude": np.abs(transformed),
               "power": np.abs(transformed) ** 2}[mode]
    port = {"type": "audio", "sample_format": "f32", "channels": channels, "block_size": frames}
    source = create_component(tmp_path, "fft_source", {
        "out": {**port, "direction": "output", "drive_port_no": 1}, "calls": scalar("int", 0),
    }, SOURCE.replace("@COUNT@", str(count)), source=True)
    capture = create_component(tmp_path, "fft_capture", {
        "mode": {**port, "direction": "input", "process": True},
        "spectrum": {**port, "type": "bulk", "layout": "halfcomplex", "frame_size": size,
                     "direction": "input", "process": True},
        "restored": {**port, "direction": "input", "process": True},
        "calls": scalar("int", 0), "last": scalar("float", 0.0), "frame": scalar("int", 0),
        "actual_values": {"name": "本次Task实际输出位型", "type": "float", "count": count * 6,
                    "kind": "probe", "access": "read", "update_policy": "immediate",
                    "readback": True, "persistent": False},
    }, CAPTURE.replace("@COUNT@", str(count)).replace("@FRAMES@", str(frames))
        .replace("@MODES@", rows(outputs.reshape(4, count)))
        .replace("@SPECTRA@", rows(spectra.reshape(4, count)))
        .replace("@TOLERANCE@", ".002f" if mode == "power" else ".0002f"))
    fft = ROOT / "components" / "orpheus" / "builtin" / "rfft" / "component.yaml"
    inverse = ROOT / "components" / "orpheus" / "builtin" / "ifft" / "component.yaml"
    manifests = [source, fft, inverse, capture]
    registry = Registry()
    for manifest in manifests:
        registry.add_manifest(manifest)
    params = {"channels": channels, "fft_size": size, "batches": batches}
    project = Project(flow_mode="task_sequence", block_size=7, graph=Graph(nodes={
        "source": Node("source", "orpheus.test.fft_source"),
        "fft": Node("fft", "orpheus.builtin.rfft", params={**params, "output_mode": mode}),
        "inverse": Node("inverse", "orpheus.builtin.ifft", params=params),
        "capture": Node("capture", "orpheus.test.fft_capture"),
    }, connections=[
        Connection(PortRef("source", "out"), PortRef("fft", "in")),
        Connection(PortRef("fft", "out"), PortRef("capture", "mode")),
        Connection(PortRef("fft", "spectrum"), PortRef("capture", "spectrum")),
        Connection(PortRef("fft", "spectrum"), PortRef("inverse", "spectrum")),
        Connection(PortRef("inverse", "out"), PortRef("capture", "restored")),
    ]))
    harness = '#include <string.h>\n' + four_entry_harness().replace(",8)", ",7)").replace("(8)", "(7)")
    harness = harness.replace('        printf("%d', SNAPSHOT.replace("@SAMPLES@", str(count * 6))
                              + '        printf("%d')
    case = compile_delivery_backends(tmp_path, registry, project, manifests, harness)
    reference = None
    for backend in ("dynamic", "generated_backend"):
        for entry in (0, 1):
            result, actual = run_backend(case, backend, mode=entry)
            assert result.returncode == 0, (backend, entry, result.stderr)
            assert [row for row in actual if len(row) == 4] == [
                [0, 6, pytest.approx(float(inputs[1, -1]), abs=1e-6), 0],
                [1, 12, pytest.approx(float(inputs[3, -1]), abs=1e-6), 7],
            ]
            patterns = [row for row in actual if len(row) == count * 6]
            assert len(patterns) == 2
            if reference is None:
                reference = patterns
            else:
                assert patterns == reference, (backend, entry)
