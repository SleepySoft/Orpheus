#include "orpheus_scalar_to_i32.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.channels=1,.block_size=1},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_I32,.channels=1,.block_size=1}
};
static const OrpheusComponentDescriptor descriptor={.id="orpheus.builtin.scalar_to_i32",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,.ports=ports,.port_count=2,.state_size=sizeof(ScalarToI32State),.alignment=8,.realtime_safe=true};
static const OrpheusComponentDescriptor* get_descriptor(void){return &descriptor;}
static int create(void** state,const OrpheusConfig* config){
    if(!state)return ORPHEUS_ERR_INVALID_ARG;
    bool external=config&&config->state_block;*state=external?config->state_block:calloc(1,sizeof(ScalarToI32State));
    if(!*state)return ORPHEUS_ERR_OUT_OF_MEMORY;
    ((ScalarToI32State*)*state)->owns_memory=!external;return 0;
}
static int destroy(void* s){if(s&&((ScalarToI32State*)s)->owns_memory)free(s);return 0;}
static int prepare(void* s,const OrpheusConfig* c){return s&&c&&!c->param_count?0:ORPHEUS_ERR_INVALID_ARG;}
static int reset(void* s){return s?0:ORPHEUS_ERR_INVALID_ARG;}
static int process(void* state,const OrpheusProcessContext* c){
    if(!state||!c||!c->inputs||!c->outputs||c->input_count<1||c->output_count<1)return ORPHEUS_ERR_INVALID_ARG;
    if(!c->frame_count)return 0;
    const OrpheusBuffer* in=c->inputs[0];OrpheusBuffer* out=c->outputs[0];
    if(!in||!in->data||in->format!=ORPHEUS_FORMAT_F32||!in->interleaved||in->channels!=1||in->frame_count!=1||in->frame_capacity<1)return ORPHEUS_ERR_INVALID_ARG;
    float v;memcpy(&v,in->data,4);
    if(!isfinite(v)||(double)v<-2147483648.0||(double)v>=2147483648.0)return ORPHEUS_ERR_INVALID_ARG;
    if(out){
        if(!out->data||out->format!=ORPHEUS_FORMAT_I32||!out->interleaved||out->channels!=1||out->frame_capacity<1)return ORPHEUS_ERR_INVALID_ARG;
        uintptr_t a=(uintptr_t)in->data,b=(uintptr_t)out->data;
        if((a>b?a-b:b-a)<4)return ORPHEUS_ERR_INVALID_ARG;
        int32_t result=(int32_t)v;memcpy(out->data,&result,4);out->frame_count=1;
    }
    return orpheus_process_next(c);
}
static const OrpheusComponentInterface api={.get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,.process=process};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void){return &api;}
