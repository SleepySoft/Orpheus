#include "capture.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="channels_a",.type=ORPHEUS_VALUE_INT},{.id="channels_b",.type=ORPHEUS_VALUE_INT},
    {.id="frames_a",.type=ORPHEUS_VALUE_INT},{.id="frames_b",.type=ORPHEUS_VALUE_INT}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.test.tensor_pair_capture",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .params=params,.param_count=4,.state_size=sizeof(TensorPairCapture),.alignment=8,.realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state,const OrpheusConfig* config) {
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(TensorPairCapture));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    ((TensorPairCapture*)*state)->owns_memory=!(config && config->state_block);
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((TensorPairCapture*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    TensorPairCapture* s=(TensorPairCapture*)state;unsigned values[]={1,1,258,129};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];unsigned p=0;
        if (!key || config->param_values[i].type!=ORPHEUS_VALUE_INT || config->param_values[i].value.i32<1)
            return ORPHEUS_ERR_INVALID_ARG;
        while(p<4 && strcmp(key,params[p].id)) ++p;
        if(p==4) return ORPHEUS_ERR_INVALID_ARG;
        values[p]=config->param_values[i].value.i32;
    }
    if ((uint64_t)values[0]*values[2]>8192 || (uint64_t)values[1]*values[3]>8192) return ORPHEUS_ERR_INVALID_ARG;
    s->channels_a=values[0];s->channels_b=values[1];s->frames_a=values[2];s->frames_b=values[3];s->calls=0;s->quantum=0;
    return ORPHEUS_OK;
}
static int reset(void* state) {
    TensorPairCapture* s=(TensorPairCapture*)state;s->calls=0;s->quantum=0;return ORPHEUS_OK;
}
static int process(void* state,const OrpheusProcessContext* context) {
    TensorPairCapture* s=(TensorPairCapture*)state;
    if (!context || !context->inputs || context->input_count<2) return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* a=context->inputs[0];const OrpheusBuffer* b=context->inputs[1];
    if (!a || !b || !a->data || !b->data || a->channels!=s->channels_a || b->channels!=s->channels_b ||
        a->frame_count!=s->frames_a || b->frame_count!=s->frames_b) return ORPHEUS_ERR_INVALID_ARG;
    memcpy(s->packed_values,a->data,(size_t)s->frames_a*s->channels_a*sizeof(float));
    memcpy(s->decoded_values,b->data,(size_t)s->frames_b*s->channels_b*sizeof(float));
    ++s->calls;s->quantum=(int32_t)context->frame_count;return ORPHEUS_OK;
}
static int get(void* state,const char* key,OrpheusValue* value) {
    TensorPairCapture* s=(TensorPairCapture*)state;value->type=ORPHEUS_VALUE_INT;
    if(!strcmp(key,"calls")) value->value.i32=s->calls;
    else if(!strcmp(key,"quantum")) value->value.i32=s->quantum;
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static int slots(void* state,const OrpheusRegistry* registry) {
    TensorPairCapture* s=(TensorPairCapture*)state;
    ORPHEUS_REG_ARRAY(registry,s,packed_values,8192,ORPHEUS_SLOT_BULK,"packed_values","张量A",ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_ARRAY(registry,s,decoded_values,8192,ORPHEUS_SLOT_BULK,"decoded_values","张量B",ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_READBACK);
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
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
