#include "array_pair.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="channels",.type=ORPHEUS_VALUE_INT},
    {.id="elements",.type=ORPHEUS_VALUE_INT},
    {.id="mode",.type=ORPHEUS_VALUE_INT},
    {.id="complex_mode",.type=ORPHEUS_VALUE_INT}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.test.array_pair",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .params=params,.param_count=4,.state_size=sizeof(ArrayPairState),.alignment=8,.realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state,const OrpheusConfig* config) {
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(ArrayPairState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    ((ArrayPairState*)*state)->owns_memory=!(config && config->state_block);
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((ArrayPairState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    ArrayPairState* s=(ArrayPairState*)state;
    unsigned channels=2,elements=129,mode=0,complex_mode=0;
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];
        if (!key || config->param_values[i].type!=ORPHEUS_VALUE_INT || config->param_values[i].value.i32<0)
            return ORPHEUS_ERR_INVALID_ARG;
        unsigned value=config->param_values[i].value.i32;
        if (!strcmp(key,"channels")) channels=value;
        else if (!strcmp(key,"elements")) elements=value;
        else if (!strcmp(key,"mode")) mode=value;
        else if (!strcmp(key,"complex_mode")) complex_mode=value;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
    if (!channels || !elements || mode>1 || complex_mode>1 || (uint64_t)channels*elements*2>8192) return ORPHEUS_ERR_INVALID_ARG;
    s->channels=channels; s->elements=elements; s->mode=mode; s->calls=0; s->quantum=0;
    s->complex_mode=complex_mode;
    return ORPHEUS_OK;
}
static int reset(void* state) {
    ArrayPairState* s=(ArrayPairState*)state; s->calls=0; s->quantum=0; return ORPHEUS_OK;
}
static int process(void* state,const OrpheusProcessContext* context) {
    ArrayPairState* s=(ArrayPairState*)state;
    unsigned count=s->channels*s->elements;
    if (!s->mode) {
        if (!context->outputs || !context->output_count || !context->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
        OrpheusBuffer* out=context->outputs[0];
        if (!out->data || out->frame_capacity<s->elements || out->channels!=s->channels) return ORPHEUS_ERR_INVALID_ARG;
        float* output=(float*)out->data;
        for (unsigned i=0;i<count;++i) output[i]=((int32_t)((i*17+(unsigned)s->calls*13)%129)-64)*0.03125f;
        out->frame_count=s->elements;
        if (s->complex_mode) {
            if (context->output_count<2 || !context->outputs[1]) return ORPHEUS_ERR_INVALID_ARG;
            OrpheusBuffer* complex=context->outputs[1];
            if (!complex->data || complex->frame_capacity<s->elements*2 || complex->channels!=s->channels)
                return ORPHEUS_ERR_INVALID_ARG;
            float* values=(float*)complex->data;
            for (unsigned i=0;i<count*2;++i) values[i]=((int32_t)((i*29+(unsigned)s->calls*7)%257)-128)*0.0078125f;
            complex->frame_count=s->elements*2;
        }
    } else {
        if (!context->inputs || context->input_count<2) return ORPHEUS_ERR_INVALID_ARG;
        if (s->complex_mode && context->input_count<3) return ORPHEUS_ERR_INVALID_ARG;
        const OrpheusBuffer* a=context->inputs[0]; const OrpheusBuffer* b=context->inputs[s->complex_mode ? 2 : 1];
        if (!a || !b || !a->data || !b->data || a->channels!=s->channels ||
            b->channels!=s->channels*(s->complex_mode ? 1 : 2) ||
            a->frame_count!=s->elements || b->frame_count!=s->elements*(s->complex_mode ? 2 : 1))
            return ORPHEUS_ERR_INVALID_ARG;
        memcpy(s->packed_values,a->data,count*sizeof(float));
        memcpy(s->decoded_values,b->data,count*2*sizeof(float));
    }
    ++s->calls; s->quantum=(int32_t)context->frame_count;
    return ORPHEUS_OK;
}
static int get(void* state,const char* key,OrpheusValue* value) {
    ArrayPairState* s=(ArrayPairState*)state; value->type=ORPHEUS_VALUE_INT;
    if (!strcmp(key,"calls")) value->value.i32=s->calls;
    else if (!strcmp(key,"quantum")) value->value.i32=s->quantum;
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static int slots(void* state,const OrpheusRegistry* registry) {
    ArrayPairState* s=(ArrayPairState*)state;
    ORPHEUS_REG_ARRAY(registry,s,packed_values,8192,ORPHEUS_SLOT_BULK,"packed_values","输入数组",ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_ARRAY(registry,s,decoded_values,8192,ORPHEUS_SLOT_BULK,"decoded_values","输出数组",ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_READBACK);
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
