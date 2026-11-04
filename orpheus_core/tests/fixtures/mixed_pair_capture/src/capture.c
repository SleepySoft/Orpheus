#include "mixed_capture.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={{.id="channels",.type=ORPHEUS_VALUE_INT},{.id="frames",.type=ORPHEUS_VALUE_INT}};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.test.mixed_pair_capture",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .params=params,.param_count=2,.state_size=sizeof(MixedPairCapture),.alignment=8,.realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(MixedPairCapture));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    ((MixedPairCapture*)*state)->owns_memory=!(config && config->state_block);
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((MixedPairCapture*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    MixedPairCapture* s=(MixedPairCapture*)state;unsigned values[]={1,1};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];unsigned p=0;
        if (!key || config->param_values[i].type!=ORPHEUS_VALUE_INT || config->param_values[i].value.i32<1)
            return ORPHEUS_ERR_INVALID_ARG;
        while(p<2 && strcmp(key,params[p].id)) ++p;
        if(p==2) return ORPHEUS_ERR_INVALID_ARG;
        values[p]=config->param_values[i].value.i32;
    }
    if ((uint64_t)values[0]*values[1]>8192) return ORPHEUS_ERR_INVALID_ARG;
    s->channels=values[0];s->frames=values[1];s->calls=0;s->quantum=0;return ORPHEUS_OK;
}
static int reset(void* state) {
    MixedPairCapture* s=(MixedPairCapture*)state;s->calls=0;s->quantum=0;return ORPHEUS_OK;
}
static int process(void* state,const OrpheusProcessContext* context) {
    MixedPairCapture* s=(MixedPairCapture*)state;
    if (!context || !context->inputs || context->input_count<2) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned p=0;p<2;++p) {
        const OrpheusBuffer* b=context->inputs[p];
        if (!b || !b->data || b->channels!=s->channels || b->frame_count!=s->frames || b->frame_capacity<s->frames ||
            !b->interleaved || b->format!=(p ? ORPHEUS_FORMAT_I32 : ORPHEUS_FORMAT_F32)) return ORPHEUS_ERR_INVALID_ARG;
    }
    size_t bytes=(size_t)s->channels*s->frames*4;
    memcpy(s->packed_values,context->inputs[0]->data,bytes);memcpy(s->decoded_values,context->inputs[1]->data,bytes);
    ++s->calls;s->quantum=(int32_t)context->frame_count;return orpheus_process_next(context);
}
static int get(void* state,const char* key,OrpheusValue* value) {
    MixedPairCapture* s=(MixedPairCapture*)state;value->type=ORPHEUS_VALUE_INT;
    if(!strcmp(key,"calls")) value->value.i32=s->calls;
    else if(!strcmp(key,"quantum")) value->value.i32=s->quantum;
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static int slots(void* state,const OrpheusRegistry* registry) {
    MixedPairCapture* s=(MixedPairCapture*)state;
    ORPHEUS_REG_ARRAY(registry,s,packed_values,8192,ORPHEUS_SLOT_BULK,"packed_values","浮点输入",ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_ARRAY(registry,s,decoded_values,8192,ORPHEUS_SLOT_BULK,"decoded_values","整数结果",ORPHEUS_VALUE_INT,.flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry,s,calls,ORPHEUS_SLOT_PROBE,"calls","调用数",ORPHEUS_VALUE_INT,.flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry,s,quantum,ORPHEUS_SLOT_PROBE,"quantum","时间量子",ORPHEUS_VALUE_INT,.flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.get_parameter=get,.register_slots=slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
