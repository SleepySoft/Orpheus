#include "component_state.h"
#include <string.h>
#include <math.h>

#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

_Static_assert(_Generic(&(((orpheus_builtin_sweep_record_State*)0)->sweep), float (*)[512]: 1, default: 0),
    "BULK sweep: storage 类型或固定容量不匹配");
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="start_freq", .name="起始频率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=2.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e+00f, .max_f32=2.000000000e+04f},
    {.id="end_freq", .name="结束频率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=2.000000000e+04f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e+00f, .max_f32=2.000000000e+04f},
    {.id="duration_s", .name="扫频时长", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="s", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=3.000000000e+02f},
    {.id="log_scale", .name="对数扫频", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=true}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="bins", .name="记录点数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=64}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=8, .max_i32=256},
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
    {.id="sweep", .name="扫频曲线", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0"}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
    {.id="progress", .name="扫频进度", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
    {.id="current_freq", .name="当前频率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.sweep_record", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=9,
    .state_size=sizeof(orpheus_builtin_sweep_record_State), .alignment=_Alignof(orpheus_builtin_sweep_record_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_sweep_record_Parameters {
    float start_freq;
    float end_freq;
    float duration_s;
    bool log_scale;
    int32_t bins;
    int32_t channels;
    float sweep[512];
    float progress;
    float current_freq;
} orpheus_builtin_sweep_record_Parameters;
static void snapshot_parameters(const orpheus_builtin_sweep_record_State* s, orpheus_builtin_sweep_record_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->start_freq=s->start_freq;
    p->end_freq=s->end_freq;
    p->duration_s=s->duration_s;
    p->log_scale=s->log_scale;
    p->bins=s->bins;
    p->channels=s->channels;
    memcpy(p->sweep, s->sweep, sizeof(p->sweep));
    p->progress=s->progress;
    p->current_freq=s->current_freq;
}
static void commit_parameters(orpheus_builtin_sweep_record_State* s, const orpheus_builtin_sweep_record_Parameters* p) {
    (void)s; (void)p;
    s->start_freq=p->start_freq;
    s->end_freq=p->end_freq;
    s->duration_s=p->duration_s;
    s->log_scale=p->log_scale;
    s->bins=p->bins;
    s->channels=p->channels;
    memcpy(s->sweep, p->sweep, sizeof(s->sweep));
    s->progress=p->progress;
    s->current_freq=p->current_freq;
}
static void default_parameters(orpheus_builtin_sweep_record_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->start_freq=2.000000000e+01f;
    p->end_freq=2.000000000e+04f;
    p->duration_s=0.000000000e+00f;
    p->log_scale=true;
    p->bins=64;
    p->channels=1;
    const float initial_sweep[512]={0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f};
    memcpy(p->sweep, initial_sweep, sizeof(p->sweep));
    p->progress=0.000000000e+00f;
    p->current_freq=0.000000000e+00f;
}
static int resource_count_sweep(const orpheus_builtin_sweep_record_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=512;
    if (dimension0<1 || (uint64_t)dimension0>512u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int validate_resource_counts(const orpheus_builtin_sweep_record_Parameters* p) {
    uint32_t count; int result;
    result=resource_count_sweep(p, &count);
    if (result!=ORPHEUS_OK) return result;
    return ORPHEUS_OK;
}
static int update_parameter(orpheus_builtin_sweep_record_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "start_freq") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e+00f || value->value.f32>2.000000000e+04f) return ORPHEUS_ERR_INVALID_ARG;
        p->start_freq=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "end_freq") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e+00f || value->value.f32>2.000000000e+04f) return ORPHEUS_ERR_INVALID_ARG;
        p->end_freq=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "duration_s") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>3.000000000e+02f) return ORPHEUS_ERR_INVALID_ARG;
        p->duration_s=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "log_scale") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->log_scale=value->value.b;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "bins") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<8 || value->value.i32>256) return ORPHEUS_ERR_INVALID_ARG;
        p->bins=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "sweep") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING || !value->value.str) return ORPHEUS_ERR_INVALID_ARG;
        const char* cursor=value->value.str;
        for (uint32_t i=0; i<512; ++i) {
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
            if (i+1<512) { if (*cursor!=',') return ORPHEUS_ERR_INVALID_ARG; ++cursor; }
            else if (*cursor) return ORPHEUS_ERR_INVALID_ARG;
            p->sweep[i]=element;
        }
        return ORPHEUS_OK;
    }
    if (strcmp(key, "progress") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->progress=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "current_freq") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->current_freq=value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_sweep_record_State* s=(orpheus_builtin_sweep_record_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "sweep")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "progress")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "current_freq")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_sweep_record_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_sweep_record_State* s=(orpheus_builtin_sweep_record_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "start_freq")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->start_freq; return ORPHEUS_OK;
    }
    if (strcmp(key, "end_freq")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->end_freq; return ORPHEUS_OK;
    }
    if (strcmp(key, "duration_s")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->duration_s; return ORPHEUS_OK;
    }
    if (strcmp(key, "log_scale")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->log_scale; return ORPHEUS_OK;
    }
    if (strcmp(key, "bins")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->bins; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "sweep")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "progress")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->progress; return ORPHEUS_OK;
    }
    if (strcmp(key, "current_freq")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->current_freq; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_sweep_record_Parameters* p, const OrpheusConfig* config) {
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
static int cleanup(orpheus_builtin_sweep_record_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_sweep_record_State* s=(orpheus_builtin_sweep_record_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_sweep_record_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_sweep_record_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_sweep_record_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_sweep_record_State* s=(orpheus_builtin_sweep_record_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_sweep_record_State* s=(orpheus_builtin_sweep_record_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_sweep_record_State* s=(orpheus_builtin_sweep_record_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_sweep_record_reset(s);
}
int orpheus_builtin_sweep_record_register_sweep(orpheus_builtin_sweep_record_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_sweep_record_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_sweep;
    if (resource_count_sweep(&current, &count_sweep)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, sweep, count_sweep, ORPHEUS_SLOT_PROBE, "sweep", "扫频曲线", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_sweep_record_State* s=(orpheus_builtin_sweep_record_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, start_freq, ORPHEUS_SLOT_SETTING, "start_freq", "起始频率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e+00f, .max_f32=2.000000000e+04f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, end_freq, ORPHEUS_SLOT_SETTING, "end_freq", "结束频率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e+00f, .max_f32=2.000000000e+04f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, duration_s, ORPHEUS_SLOT_SETTING, "duration_s", "扫频时长", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="s", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=3.000000000e+02f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, log_scale, ORPHEUS_SLOT_SETTING, "log_scale", "对数扫频", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, bins, ORPHEUS_SLOT_SETTING, "bins", "记录点数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=8, .max_i32=256)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    int result_sweep=orpheus_builtin_sweep_record_register_sweep(s, reg);
    if (result_sweep!=ORPHEUS_OK) return result_sweep;
    if (ORPHEUS_REG_SLOT(reg, s, progress, ORPHEUS_SLOT_PROBE, "progress", "扫频进度", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, current_freq, ORPHEUS_SLOT_PROBE, "current_freq", "当前频率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
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
    orpheus_builtin_sweep_record_State* s=(orpheus_builtin_sweep_record_State*)state;
    orpheus_builtin_sweep_record_IO io={0};
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
    return orpheus_builtin_sweep_record_algorithm(s, &io, ctx);
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
