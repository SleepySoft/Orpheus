#include "orpheus_bulk_db.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
#define INTEGER(key,label,initial,low,high,policy,signature) \
    {.id=key,.name=label,.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=initial}, \
     .min_i32=low,.max_i32=high,.update_policy=policy,.affects_signature=signature}
    INTEGER("channels","通道数",1,1,128,ORPHEUS_UPDATE_RESTART_REQUIRED,true),
    INTEGER("elements","每通道元素数",1,1,65536,ORPHEUS_UPDATE_RESTART_REQUIRED,true),
    INTEGER("mode","换算方向",0,0,1,ORPHEUS_UPDATE_BLOCK_BOUNDARY,false),
    INTEGER("quantity","物理量",0,0,1,ORPHEUS_UPDATE_BLOCK_BOUNDARY,false),
#undef INTEGER
    {.id="epsilon",.name="正向对数的非负偏置",.type=ORPHEUS_VALUE_FLOAT,
     .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=2.22044605e-16f},
     .min_f32=0,.max_f32=FLT_MAX,.update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY}
};
#define PORT(key,dir) {.id=key,.direction=dir,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
static const OrpheusPort ports[]={PORT("in",ORPHEUS_PORT_INPUT),PORT("out",ORPHEUS_PORT_OUTPUT)};
#undef PORT
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.bulk_db",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=2,.params=params,.param_count=5,.state_size=sizeof(BulkDbState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(BulkDbState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(BulkDbState));
    ((BulkDbState*)*state)->owns_memory=!(config && config->state_block);return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((BulkDbState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static bool value_valid(unsigned p,const OrpheusValue* value) {
    if (!value) return false;
    if (p==4) return value->type==ORPHEUS_VALUE_FLOAT && isfinite(value->value.f32) && value->value.f32>=0;
    return p<4 && value->type==ORPHEUS_VALUE_INT && value->value.i32>=params[p].min_i32 && value->value.i32<=params[p].max_i32;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t values[]={1,1,0,0};float epsilon=2.22044605e-16f;
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=0;while(p<5 && strcmp(key,params[p].id)) ++p;
        if (p==5 || !value_valid(p,value)) return ORPHEUS_ERR_INVALID_ARG;
        if (p<4) values[p]=(uint32_t)value->value.i32;
        else epsilon=value->value.f32;
    }
    BulkDbState* s=(BulkDbState*)state;
    s->channels=values[0];s->elements=values[1];s->mode=values[2];s->quantity=values[3];s->epsilon=epsilon;return ORPHEUS_OK;
}
static int reset(void* state) {return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;}
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    unsigned p=2;while(p<5 && strcmp(key,params[p].id)) ++p;
    if (p==5) return ORPHEUS_ERR_UNSUPPORTED;
    if (!value_valid(p,value)) return ORPHEUS_ERR_INVALID_ARG;
    BulkDbState* s=(BulkDbState*)state;
    if (p==2) s->mode=(uint32_t)value->value.i32;
    else if (p==3) s->quantity=(uint32_t)value->value.i32;
    else s->epsilon=value->value.f32;
    return ORPHEUS_OK;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const BulkDbState* s=(const BulkDbState*)state;
    if (!strcmp(key,"epsilon")) {value->type=ORPHEUS_VALUE_FLOAT;value->value.f32=s->epsilon;}
    else {
        value->type=ORPHEUS_VALUE_INT;
        if (!strcmp(key,"channels")) value->value.i32=(int32_t)s->channels;
        else if (!strcmp(key,"elements")) value->value.i32=(int32_t)s->elements;
        else if (!strcmp(key,"mode")) value->value.i32=(int32_t)s->mode;
        else if (!strcmp(key,"quantity")) value->value.i32=(int32_t)s->quantity;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
    return ORPHEUS_OK;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned elements,bool input) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->channels==channels && b->frame_capacity>=elements && (!input || b->frame_count==elements);
}
static bool overlap(const void* a,const void* b,size_t bytes) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y ? x-y<bytes : y-x<bytes;
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs || context->input_count<1 || context->output_count<1)
        return ORPHEUS_ERR_INVALID_ARG;
    const BulkDbState* s=(const BulkDbState*)state;
    if (!s->channels || !s->elements) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    const OrpheusBuffer* in=context->inputs[0];OrpheusBuffer* out=context->outputs[0];
    if (!valid(in,s->channels,s->elements,true)) return ORPHEUS_ERR_INVALID_ARG;
    if (!out) return orpheus_process_next(context);
    size_t count=(size_t)s->channels*s->elements,bytes=count*sizeof(float);
    if (!valid(out,s->channels,s->elements,false) || overlap(in->data,out->data,bytes)) return ORPHEUS_ERR_INVALID_ARG;
    const unsigned char* src=(const unsigned char*)in->data;unsigned char* dst=(unsigned char*)out->data;
    const float factor=s->quantity ? 20.0f : 10.0f;
    for (size_t i=0;i<count;++i) {
        float value;memcpy(&value,src+i*sizeof(float),sizeof(float));
        if (s->mode) {
            volatile float exponent=value*2.30258512f;
            exponent=exponent/factor;value=expf(exponent);
        } else {
            volatile float biased=value+s->epsilon;
            value=log10f(biased)*factor;
        }
        memcpy(dst+i*sizeof(float),&value,sizeof(float));
    }
    out->frame_count=s->elements;return orpheus_process_next(context);
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.set_parameter=set_parameter,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
