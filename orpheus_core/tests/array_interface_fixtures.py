"""真实接口调用图使用的生成型数组源与双输入采集器。"""

from test_interface_delivery import create_component, scalar


SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_array_source_algorithm(orpheus_test_array_source_State* s,
    orpheus_test_array_source_IO* io, const OrpheusProcessContext* ctx) {
    if(!io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    for(int32_t i=0;i<s->channels*s->elements;i++)
        io->out.samples[i]=@VALUE@;
    io->out.buffer->frame_count=(uint32_t)s->elements;
    s->calls++;
    return orpheus_test_array_source_emit_out(ctx,&io->out);
}
'''

PAIRED_SOURCE = r'''
#include "component_state.h"
#include "component_api.h"
int orpheus_test_array_source_algorithm(orpheus_test_array_source_State* s,
    orpheus_test_array_source_IO* io, const OrpheusProcessContext* ctx) {
    if(!io->out.buffer || !io->other.buffer) return ORPHEUS_ERR_INVALID_ARG;
    int32_t tick=s->calls++;
    for(int32_t i=0;i<s->channels*s->elements;i++)
        io->out.samples[i]=@VALUE@;
    io->out.buffer->frame_count=io->other.buffer->frame_count=(uint32_t)s->elements;
    int result=orpheus_test_array_source_emit_out(ctx,&io->out);
    if(result) return result;
    for(int32_t i=0;i<s->channels*s->elements;i++)
        io->other.samples[i]=((i*11+tick*7)%97-48)*0.0625f;
    return orpheus_test_array_source_emit_other(ctx,&io->other);
}
'''

CAPTURE = r'''
#include "component_state.h"
#include "component_api.h"
#include <string.h>
int orpheus_test_array_capture_algorithm(orpheus_test_array_capture_State* s,
    orpheus_test_array_capture_IO* io, const OrpheusProcessContext* ctx) {
    if(ctx->trigger_input!=ORPHEUS_TEST_ARRAY_CAPTURE_INPUT_A
        && ctx->trigger_input!=ORPHEUS_TEST_ARRAY_CAPTURE_INPUT_B)
        return ORPHEUS_ERR_INVALID_ARG;
    if(!io->a.buffer || !io->b.buffer
        || io->a.buffer->frame_count!=(uint32_t)s->frames_a
        || io->b.buffer->frame_count!=(uint32_t)s->frames_b)
        return ORPHEUS_ERR_INVALID_ARG;
    memcpy(s->algorithm.packed_values,io->a.samples,
        (size_t)s->channels_a*s->frames_a*sizeof(float));
    memcpy(s->algorithm.decoded_values,io->b.samples,
        (size_t)s->channels_b*s->frames_b*sizeof(float));
    s->calls++;
    s->quantum=(int32_t)ctx->frame_count;
    return ORPHEUS_OK;
}
'''


def register_array_interfaces(directory, registry, *, capture_b_process=True, paired_source=False,
                              source_audio=False):
    def parameter(default, maximum):
        return {**scalar("int", default), "range": [1, maximum],
                "affects_signature": True, "update_policy": "restart_required"}

    def port(direction, channels, frames, **fields):
        return {"direction": direction, "type": "bulk", "sample_format": "f32",
                "channels": f"param:{channels}", "block_size": f"param:{frames}", **fields}

    source_interfaces = {
        "out": port("output", "channels", "elements", drive_port_no=1),
        "channels": parameter(1, 128), "elements": parameter(1, 65536),
        "calls": scalar("int", 0),
    }
    if paired_source:
        source_interfaces["other"] = port("output", "channels", "elements", drive_port_no=2)
    if source_audio:
        source_interfaces["out"]["type"] = "audio"
    call = "tick" if paired_source else "s->calls"
    value = f"((i*17+{call}*13)%50001-25000)*0.000030517578125f" if source_audio else (
        f"((i*17+{call}*13)%129-64)*0.03125f")
    source_code = (PAIRED_SOURCE if paired_source else SOURCE).replace("@VALUE@", value)
    source = create_component(directory, "array_source", source_interfaces, source_code, source=True)
    interfaces = {
        "a": port("input", "channels_a", "frames_a", process=True),
        "b": port("input", "channels_b", "frames_b", process=capture_b_process),
        "channels_a": parameter(1, 128), "channels_b": parameter(1, 128),
        "frames_a": parameter(1, 65536), "frames_b": parameter(1, 65536),
        "calls": scalar("int", 0), "quantum": scalar("int", 0),
    }
    for key, suffix in (("packed_values", "a"), ("decoded_values", "b")):
        interfaces[key] = {
            "type": "string", "default": "", "kind": "bulk", "readback": True,
            "persistent": False, "update_policy": "restart_required",
            "shape": [f"param:frames_{suffix}", f"param:channels_{suffix}"],
            "bulk": {"sample_format": "f32", "capacity": 8192,
                     "storage": f"algorithm.{key}"},
        }
    capture = create_component(directory, "array_capture", interfaces, CAPTURE)
    (capture.parent / "user" / "algorithm.h").write_text(
        "#pragma once\n"
        "typedef struct orpheus_test_array_capture_AlgorithmState {\n"
        "    float packed_values[8192], decoded_values[8192];\n"
        "} orpheus_test_array_capture_AlgorithmState;\n", encoding="utf-8")
    for manifest in (source, capture):
        registry.add_manifest(manifest)
