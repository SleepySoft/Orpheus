#include "orpheus_call_snapshot.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {.id="targets",.name="同次调用40项目标",.type=ORPHEUS_VALUE_FLOAT,.default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY},
    {.id="initial_targets",.name="初始40项目标",.type=ORPHEUS_VALUE_STRING,
     .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED},
    {.id="sequence",.name="32位序列位型",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY},
    {.id="coefficient",.name="输入斜坡系数",.type=ORPHEUS_VALUE_FLOAT,.default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY}
};
#define OUTPUT(ID,N,FMT) {.id=ID,.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=FMT,.channels=N,.block_size=1}
static const OrpheusPort ports[] = {
    OUTPUT("drv_targets",20,ORPHEUS_FORMAT_F32),OUTPUT("pax_targets",20,ORPHEUS_FORMAT_F32),
    OUTPUT("sequence",1,ORPHEUS_FORMAT_I32),OUTPUT("coefficient",1,ORPHEUS_FORMAT_F32)
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.call_snapshot",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=4,.params=params,.param_count=4,.state_size=sizeof(CallSnapshotState),.alignment=8,.realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    bool external=config && config->state_block;
    *state=external?config->state_block:calloc(1,sizeof(CallSnapshotState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(CallSnapshotState));((CallSnapshotState*)*state)->owns_memory=!external;return 0;
}
static int destroy(void* state) {if (state && ((CallSnapshotState*)state)->owns_memory) free(state);return 0;}
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    CallSnapshotState* s=state;
    if (!strcmp(key,"sequence")) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        s->sequence=value->value.i32;return 0;
    }
    if (!strcmp(key,"coefficient")) {
        if (value->type!=ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        s->coefficient=value->value.f32;return 0;
    }
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    CallSnapshotState* s=state;
    if (!strcmp(key,"sequence")) {value->type=ORPHEUS_VALUE_INT;value->value.i32=s->sequence;return 0;}
    if (!strcmp(key,"coefficient")) {value->type=ORPHEUS_VALUE_FLOAT;value->value.f32=s->coefficient;return 0;}
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values))) return ORPHEUS_ERR_INVALID_ARG;
    CallSnapshotState next={.owns_memory=((CallSnapshotState*)state)->owns_memory};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* k=config->param_ids[i];const OrpheusValue* v=config->param_values+i;
        if (!k) return ORPHEUS_ERR_INVALID_ARG;
        if (!strcmp(k,"initial_targets")) {
            if (v->type!=ORPHEUS_VALUE_STRING || !v->value.str) return ORPHEUS_ERR_INVALID_ARG;
            const char* cursor=v->value.str;
            for (unsigned n=0;n<40;++n) {
                char* end;float x=strtof(cursor,&end);
                if (end==cursor || !isfinite(x)) return ORPHEUS_ERR_INVALID_ARG;
                next.targets[n]=next.initial[n]=x;cursor=end;
                while (*cursor==' ' || *cursor=='\t') ++cursor;
                if (n<39) {if (*cursor!=',') return ORPHEUS_ERR_INVALID_ARG;++cursor;}
            }
            if (*cursor) return ORPHEUS_ERR_INVALID_ARG;
        } else if (!strcmp(k,"targets")) {
            if (v->type!=ORPHEUS_VALUE_FLOAT || v->value.f32!=0) return ORPHEUS_ERR_INVALID_ARG;
        } else {int r=set_parameter(&next,k,v);if (r) return r;}
    }
    *(CallSnapshotState*)state=next;return 0;
}
static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    /* 控制输入不是算法历史：暖复位保留当前输入，与原独立RampProcessing_Init一致。 */
    return 0;
}
static int register_slots(void* state,const OrpheusRegistry* reg) {
    if (!state || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    CallSnapshotState* s=state;
    ORPHEUS_REG_ARRAY(reg,s,targets,40,ORPHEUS_SLOT_BULK,"targets","同次调用目标",ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_DOUBLE_BUFFERED);
    ORPHEUS_REG_SLOT(reg,s,sequence,ORPHEUS_SLOT_SETTING,"sequence","32位序列位型",ORPHEUS_VALUE_INT,
        .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,coefficient,ORPHEUS_SLOT_SETTING,"coefficient","输入斜坡系数",ORPHEUS_VALUE_FLOAT,
        .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.flags=ORPHEUS_SLOT_READBACK);
    return 0;
}
static int process(void* state,const OrpheusProcessContext* c) {
    if (!state || !c || !c->outputs || c->output_count<4) return ORPHEUS_ERR_INVALID_ARG;
    if (!c->frame_count) return orpheus_process_next(c);
    const CallSnapshotState* s=state;
    /* 单一处理入口先拷贝全部输入；续调中的写入只影响下次调用。 */
    float snapshot[40],coefficient=s->coefficient;int32_t sequence=s->sequence;
    memcpy(snapshot,s->targets,sizeof(snapshot));
    const void* values[]={snapshot,snapshot+20,&sequence,&coefficient};
    for (unsigned i=0;i<4;++i) if (c->outputs[i]) {
        OrpheusBuffer* b=c->outputs[i];unsigned n=i<2?20:1;
        if (!b->data || b->channels!=n || b->format!=(i==2?ORPHEUS_FORMAT_I32:ORPHEUS_FORMAT_F32) ||
            !b->interleaved || b->frame_capacity<1) return ORPHEUS_ERR_INVALID_ARG;
    }
    for (unsigned i=0;i<4;++i) if (c->outputs[i]) {
        memcpy(c->outputs[i]->data,values[i],i<2?80:4);c->outputs[i]->frame_count=1;
    }
    return orpheus_process_next(c);
}
static const OrpheusComponentInterface api = {
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,.process=process,
    .set_parameter=set_parameter,.get_parameter=get_parameter,.register_slots=register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &api;}
