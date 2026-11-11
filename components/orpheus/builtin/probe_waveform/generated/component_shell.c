#include "component_state.h"
#include <string.h>
#include <math.h>

#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

_Static_assert(_Generic(&(((orpheus_builtin_probe_waveform_State*)0)->waveform), float (*)[1023]: 1, default: 0),
    "BULK waveform: storage 类型或固定容量不匹配");
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
    {.id="waveform", .name="波形", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0"}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.probe_waveform", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=2,
    .state_size=sizeof(orpheus_builtin_probe_waveform_State), .alignment=_Alignof(orpheus_builtin_probe_waveform_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_probe_waveform_Parameters {
    int32_t channels;
    float waveform[1023];
} orpheus_builtin_probe_waveform_Parameters;
static void snapshot_parameters(const orpheus_builtin_probe_waveform_State* s, orpheus_builtin_probe_waveform_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    memcpy(p->waveform, s->waveform, sizeof(p->waveform));
}
static void commit_parameters(orpheus_builtin_probe_waveform_State* s, const orpheus_builtin_probe_waveform_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    memcpy(s->waveform, p->waveform, sizeof(s->waveform));
}
static void default_parameters(orpheus_builtin_probe_waveform_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=2;
    const float initial_waveform[1023]={0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f};
    memcpy(p->waveform, initial_waveform, sizeof(p->waveform));
}
static int resource_count_waveform(const orpheus_builtin_probe_waveform_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=1023;
    if (dimension0<1 || (uint64_t)dimension0>1023u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int validate_resource_counts(const orpheus_builtin_probe_waveform_Parameters* p) {
    uint32_t count; int result;
    result=resource_count_waveform(p, &count);
    if (result!=ORPHEUS_OK) return result;
    return ORPHEUS_OK;
}
static int update_parameter(orpheus_builtin_probe_waveform_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "waveform") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING || !value->value.str) return ORPHEUS_ERR_INVALID_ARG;
        const char* cursor=value->value.str;
        for (uint32_t i=0; i<1023; ++i) {
            while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
            const char* start=cursor;
            char* end; errno=0;
            double number=strtod(cursor, &end);
            if (end==cursor || errno==ERANGE || !isfinite(number) ||
                number < -3.4028234663852886e+38 || number > 3.4028234663852886e+38) return ORPHEUS_ERR_INVALID_ARG;
            float element=(float)number;
            cursor=end;
            (void)start;
            while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
            if (i+1<1023) { if (*cursor!=',') return ORPHEUS_ERR_INVALID_ARG; ++cursor; }
            else if (*cursor) return ORPHEUS_ERR_INVALID_ARG;
            p->waveform[i]=element;
        }
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_probe_waveform_State* s=(orpheus_builtin_probe_waveform_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "waveform")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_probe_waveform_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_probe_waveform_State* s=(orpheus_builtin_probe_waveform_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "waveform")==0) return ORPHEUS_ERR_UNSUPPORTED;
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_probe_waveform_Parameters* p, const OrpheusConfig* config) {
    if (!config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    default_parameters(p);
    bool explicit_channels=false;
    for (uint32_t i=0; i<config->param_count; ++i)
        if (config->param_ids[i] && strcmp(config->param_ids[i], "channels")==0) explicit_channels=true;
    if (!explicit_channels && config->channels) {
        if (config->channels>INT32_MAX) return ORPHEUS_ERR_INVALID_ARG;
        OrpheusValue channels={.type=ORPHEUS_VALUE_INT,.value.i32=(int32_t)config->channels};
        int result=update_parameter(p, "channels", &channels);
        if (result!=ORPHEUS_OK) return result;
    }
    for (uint32_t i=0; i<config->param_count; ++i) {
        int result=update_parameter(p, config->param_ids[i], &config->param_values[i]);
        if (result!=ORPHEUS_OK) return result;
    }
    return validate_resource_counts(p);
}
static int cleanup(orpheus_builtin_probe_waveform_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_probe_waveform_State* s=(orpheus_builtin_probe_waveform_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_probe_waveform_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_probe_waveform_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_probe_waveform_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_probe_waveform_State* s=(orpheus_builtin_probe_waveform_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_probe_waveform_State* s=(orpheus_builtin_probe_waveform_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_probe_waveform_State* s=(orpheus_builtin_probe_waveform_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_probe_waveform_reset(s);
}
int orpheus_builtin_probe_waveform_register_waveform(orpheus_builtin_probe_waveform_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_probe_waveform_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_waveform;
    if (resource_count_waveform(&current, &count_waveform)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, waveform, count_waveform, ORPHEUS_SLOT_PROBE, "waveform", "波形", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_probe_waveform_State* s=(orpheus_builtin_probe_waveform_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    int result_waveform=orpheus_builtin_probe_waveform_register_waveform(s, reg);
    if (result_waveform!=ORPHEUS_OK) return result_waveform;
    return ORPHEUS_OK;
}
static int valid_buffer(const OrpheusBuffer* buffer, uint32_t channels,
        OrpheusSampleFormat format, size_t sample_size, size_t alignment) {
    return !buffer || (buffer->format == format && buffer->interleaved
        && buffer->channels == channels && channels > 0
        && buffer->frame_count <= buffer->frame_capacity
        && (!buffer->frame_capacity || buffer->data)
        && (!buffer->data || (uintptr_t)buffer->data % alignment == 0)
        && (size_t)buffer->frame_capacity <= SIZE_MAX / sample_size / channels);
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    orpheus_builtin_probe_waveform_State* s=(orpheus_builtin_probe_waveform_State*)state;
    orpheus_builtin_probe_waveform_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    io.in.input_index=0;
    io.in.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.in.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.samples=io.in.buffer ? (const float*)io.in.buffer->data : NULL;
    io.out.active=true;
    io.out.drive_port_no=1u;
    io.out.buffer=(ctx->outputs && ctx->output_count > (0)) ? ctx->outputs[0] : NULL;
    if (!valid_buffer(io.out.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.samples=io.out.buffer ? (float*)io.out.buffer->data : NULL;
    if (io.out.buffer) {
        if (ctx->frame_count > io.out.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.out.buffer->frame_count=ctx->frame_count;
    }
    io.out.delivery=(OrpheusDelivery){.buffer=io.out.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_probe_waveform_algorithm(s, &io, ctx);
}
static const OrpheusComponentInterface interface = {
    .get_descriptor=get_descriptor, .create=create, .destroy=destroy,
    .prepare=prepare, .reset=reset, .process=process,
    .set_parameter=set_parameter, .get_parameter=get_parameter, .register_slots=register_slots,
    .hook=NULL
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
