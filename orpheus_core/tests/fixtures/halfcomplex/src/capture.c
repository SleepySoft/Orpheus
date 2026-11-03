#include "capture.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="channels",.type=ORPHEUS_VALUE_INT},
    {.id="fft_size",.type=ORPHEUS_VALUE_INT},
    {.id="batches",.type=ORPHEUS_VALUE_INT},
    {.id="bins",.type=ORPHEUS_VALUE_INT}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.test.halfcomplex_capture",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .params=params,.param_count=4,
    .state_size=sizeof(HalfcomplexCapture),.alignment=8,.realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state,const OrpheusConfig* config) {
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(HalfcomplexCapture));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    ((HalfcomplexCapture*)*state)->owns_memory=!(config && config->state_block);
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((HalfcomplexCapture*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    HalfcomplexCapture* s=(HalfcomplexCapture*)state;
    unsigned channels=2,fft_size=256,batches=1,bins=129;
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];
        if (!key || config->param_values[i].type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        int32_t value=config->param_values[i].value.i32;
        if (value<1) return ORPHEUS_ERR_INVALID_ARG;
        if (!strcmp(key,"channels")) channels=value;
        else if (!strcmp(key,"fft_size")) fft_size=value;
        else if (!strcmp(key,"batches")) batches=value;
        else if (!strcmp(key,"bins")) bins=value;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
    if ((uint64_t)channels*fft_size*batches>8192 || (uint64_t)channels*bins*2*batches>8192)
        return ORPHEUS_ERR_INVALID_ARG;
    s->channels=channels; s->packed_frames=fft_size*batches; s->decoded_frames=bins*2*batches;
    s->calls=0; s->quantum=0;
    return ORPHEUS_OK;
}
static int reset(void* state) {
    HalfcomplexCapture* s=(HalfcomplexCapture*)state; s->calls=0; s->quantum=0;
    return ORPHEUS_OK;
}
static int process(void* state,const OrpheusProcessContext* context) {
    HalfcomplexCapture* s=(HalfcomplexCapture*)state;
    if (!context || !context->inputs || context->input_count<2) return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* decoded=context->inputs[0];
    const OrpheusBuffer* packed=context->inputs[1];
    const OrpheusBuffer* spectrum=context->input_count>2 ? context->inputs[2] : NULL;
    if (packed && spectrum) return ORPHEUS_ERR_INVALID_ARG;
    if (!packed) packed=spectrum;
    if (!decoded || !packed || !decoded->data || !packed->data ||
        decoded->channels!=s->channels || packed->channels!=s->channels ||
        decoded->frame_count!=s->decoded_frames || packed->frame_count!=s->packed_frames)
        return ORPHEUS_ERR_INVALID_ARG;
    memcpy(s->decoded_values,decoded->data,s->decoded_frames*s->channels*sizeof(float));
    memcpy(s->packed_values,packed->data,s->packed_frames*s->channels*sizeof(float));
    ++s->calls; s->quantum=(int32_t)context->frame_count;
    return ORPHEUS_OK;
}
static int get(void* state,const char* key,OrpheusValue* value) {
    HalfcomplexCapture* s=(HalfcomplexCapture*)state;
    value->type=ORPHEUS_VALUE_INT;
    if (!strcmp(key,"calls")) value->value.i32=s->calls;
    else if (!strcmp(key,"quantum")) value->value.i32=s->quantum;
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static int slots(void* state,const OrpheusRegistry* registry) {
    HalfcomplexCapture* s=(HalfcomplexCapture*)state;
    ORPHEUS_REG_ARRAY(registry,s,packed_values,8192,ORPHEUS_SLOT_BULK,"packed_values","原始槽位",
                     ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_ARRAY(registry,s,decoded_values,8192,ORPHEUS_SLOT_BULK,"decoded_values","解包槽位",
                     ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_READBACK);
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
