#include "orpheus_host_audio.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
#ifndef ORPHEUS_BUFFER_SOURCE
    {.id="mode", .name="方向", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0}, .min_i32=0,.max_i32=1,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED},
#endif
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2}, .min_i32=1,.max_i32=64,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="ready", .name="宿主已提交完整新块", .type=ORPHEUS_VALUE_BOOL,
     .default_value={.type=ORPHEUS_VALUE_BOOL,.value.b=false},
     .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true},
#ifndef ORPHEUS_BUFFER_SOURCE
    {.id="valid", .name="已捕获输出块", .type=ORPHEUS_VALUE_BOOL,
     .default_value={.type=ORPHEUS_VALUE_BOOL,.value.b=false},
     .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true},
#endif
    {.id="missing_blocks", .name="缺失输入块次数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},
     .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true}
};
static const OrpheusPort ports[] = {
#ifndef ORPHEUS_BUFFER_SOURCE
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,
     .is_variable=true,.channels_param="channels"},
#endif
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,
     .is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor = {
#ifdef ORPHEUS_BUFFER_SOURCE
    .id="orpheus.builtin.buffer_source",.version="1.1.0",.abi_version=ORPHEUS_ABI_VERSION,
#else
    .id="orpheus.builtin.host_audio",.version="1.1.0",.abi_version=ORPHEUS_ABI_VERSION,
#endif
    .ports=ports,.port_count=sizeof(ports)/sizeof(ports[0]),
    .params=params,.param_count=sizeof(params)/sizeof(params[0]),.state_size=sizeof(HostAudioState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    bool external=config && config->state_block;
    *state=external ? config->state_block : calloc(1,sizeof(HostAudioState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(HostAudioState));((HostAudioState*)*state)->owns_memory=!external;
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((HostAudioState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    HostAudioState* s=(HostAudioState*)state;
    memset(s->data,0,sizeof(s->data));s->ready=false;s->valid=false;s->missing_blocks=0;
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || !config->block_size ||
        (config->param_count && (!config->param_ids || !config->param_values))) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t channels=2,mode=0;
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* v=config->param_values+i;
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        if (!strcmp(key,"channels")) {
            if (v->type!=ORPHEUS_VALUE_INT || v->value.i32<1 || v->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
            channels=(uint32_t)v->value.i32;
#ifndef ORPHEUS_BUFFER_SOURCE
        } else if (!strcmp(key,"mode")) {
            if (v->type!=ORPHEUS_VALUE_INT || v->value.i32<0 || v->value.i32>1) return ORPHEUS_ERR_INVALID_ARG;
            mode=(uint32_t)v->value.i32;
#endif
        } else if (!strcmp(key,"ready") || !strcmp(key,"valid")) {
            if (v->type!=ORPHEUS_VALUE_BOOL || v->value.b) return ORPHEUS_ERR_INVALID_ARG;
        } else if (!strcmp(key,"missing_blocks")) {
            if (v->type!=ORPHEUS_VALUE_INT || v->value.i32) return ORPHEUS_ERR_INVALID_ARG;
        } else return ORPHEUS_ERR_INVALID_ARG;
    }
    if (channels>ORPHEUS_HOST_AUDIO_CAPACITY/config->block_size) return ORPHEUS_ERR_INVALID_ARG;
    HostAudioState* s=(HostAudioState*)state;s->channels=channels;s->frames=config->block_size;s->mode=mode;
    return reset(state);
}
static int set_parameter(void* state,const char* key,const OrpheusValue* v) {
    if (!state || !key || !v) return ORPHEUS_ERR_INVALID_ARG;
    HostAudioState* s=(HostAudioState*)state;
    if (strcmp(key,"ready") || s->mode) return ORPHEUS_ERR_UNSUPPORTED;
    if (v->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
    s->ready=v->value.b;return ORPHEUS_OK;
}
static int get_parameter(void* state,const char* key,OrpheusValue* v) {
    if (!state || !key || !v) return ORPHEUS_ERR_INVALID_ARG;
    const HostAudioState* s=(const HostAudioState*)state;
    if (!strcmp(key,"ready") || !strcmp(key,"valid")) {
        v->type=ORPHEUS_VALUE_BOOL;v->value.b=!strcmp(key,"ready") ? s->ready : s->valid;return ORPHEUS_OK;
    }
    v->type=ORPHEUS_VALUE_INT;
    if (!strcmp(key,"mode")) v->value.i32=(int32_t)s->mode;
    else if (!strcmp(key,"channels")) v->value.i32=(int32_t)s->channels;
    else if (!strcmp(key,"missing_blocks")) v->value.i32=s->missing_blocks;
    else return ORPHEUS_ERR_UNSUPPORTED;
    return ORPHEUS_OK;
}
static bool valid(const OrpheusBuffer* b,const HostAudioState* s,bool input) {
    return b && b->data && b->channels==s->channels && b->format==ORPHEUS_FORMAT_F32 &&
        b->interleaved && b->frame_capacity>=s->frames && (!input || b->frame_count==s->frames);
}
static bool overlaps(const void* a,const void* b,size_t bytes) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y ? x-y<bytes : y-x<bytes;
}
static int process(void* state,const OrpheusProcessContext* ctx) {
    if (!state || !ctx || !ctx->outputs || ctx->output_count<1)
        return ORPHEUS_ERR_INVALID_ARG;
#ifndef ORPHEUS_BUFFER_SOURCE
    if (!ctx->inputs || ctx->input_count<1) return ORPHEUS_ERR_INVALID_ARG;
#endif
    if (!ctx->frame_count) return ORPHEUS_OK;
    HostAudioState* s=(HostAudioState*)state;
    if (!s->frames || !s->channels || ctx->frame_count!=s->frames) return ORPHEUS_ERR_INVALID_ARG;
#ifdef ORPHEUS_BUFFER_SOURCE
    const OrpheusBuffer* in=NULL;
#else
    const OrpheusBuffer* in=ctx->inputs[0];
#endif
    OrpheusBuffer* out=ctx->outputs[0];
    size_t bytes=(size_t)s->frames*s->channels*sizeof(float);
    if ((out && (!valid(out,s,false) || overlaps(out->data,s->data,bytes))) ||
        (in && (!valid(in,s,true) || overlaps(in->data,s->data,bytes))) ||
        (in && out && overlaps(in->data,out->data,bytes))) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->mode) {
        if (in && s->ready) return ORPHEUS_ERR_INVALID_ARG;
        if (in) {
            memcpy(s->data,in->data,bytes);
        } else if (!s->ready) {
            if (s->missing_blocks<INT_MAX) ++s->missing_blocks;
            return ORPHEUS_ERR_PROCESSING;
        }
        if (out) memcpy(out->data,s->data,bytes);
        s->ready=false;
    } else {
        if (!in) return ORPHEUS_ERR_INVALID_ARG;
        memcpy(s->data,in->data,bytes);s->valid=true;
        if (out) memcpy(out->data,in->data,bytes);
    }
    if (out) out->frame_count=s->frames;
    return orpheus_process_next(ctx);
}
static int register_slots(void* state,const OrpheusRegistry* registry) {
    if (!state || !registry) return ORPHEUS_ERR_INVALID_ARG;
    HostAudioState* s=(HostAudioState*)state;
    ORPHEUS_REG_ARRAY(registry,s,data,s->frames*s->channels,ORPHEUS_SLOT_BULK,
        "samples","交错音频块",ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK | (s->mode ? 0 : ORPHEUS_SLOT_DOUBLE_BUFFERED));
    if (!s->mode) {
        ORPHEUS_REG_SLOT(registry,s,ready,ORPHEUS_SLOT_SETTING,"ready","宿主已提交完整新块",
            ORPHEUS_VALUE_BOOL,.update_policy=ORPHEUS_UPDATE_IMMEDIATE,.flags=ORPHEUS_SLOT_READBACK);
    }
#ifndef ORPHEUS_BUFFER_SOURCE
    ORPHEUS_REG_SLOT(registry,s,valid,ORPHEUS_SLOT_PROBE,"valid","已捕获输出块",ORPHEUS_VALUE_BOOL,.flags=ORPHEUS_SLOT_READBACK);
#endif
    ORPHEUS_REG_SLOT(registry,s,missing_blocks,ORPHEUS_SLOT_PROBE,"missing_blocks","缺失输入块次数",
        ORPHEUS_VALUE_INT,.flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.set_parameter=set_parameter,.get_parameter=get_parameter,.register_slots=register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
