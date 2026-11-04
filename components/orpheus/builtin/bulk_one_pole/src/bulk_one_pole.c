#include "orpheus_bulk_one_pole.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="channels",.name="通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="elements",.name="每通道元素数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .min_i32=1,.max_i32=65536,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="alpha",.name="历史系数",.type=ORPHEUS_VALUE_FLOAT,.default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.5f},
     .min_f32=0,.max_f32=1,.update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY},
    {.id="absolute_input",.name="输入先取绝对值",.type=ORPHEUS_VALUE_BOOL,.default_value={.type=ORPHEUS_VALUE_BOOL,.value.b=false},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY}
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="update",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_I32,.channels=1},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.bulk_one_pole",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .params=params,.param_count=4,.ports=ports,.port_count=3,.state_size=sizeof(BulkOnePoleState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(BulkOnePoleState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(BulkOnePoleState));
    ((BulkOnePoleState*)*state)->owns_memory=!(config && config->state_block);return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (!state) return ORPHEUS_OK;
    BulkOnePoleState* s=(BulkOnePoleState*)state;free(s->history);s->history=NULL;
    if (s->owns_memory) free(state);
    return ORPHEUS_OK;
}
static bool alpha_valid(const OrpheusValue* value) {
    return value && value->type==ORPHEUS_VALUE_FLOAT && isfinite(value->value.f32) && value->value.f32>=0 && value->value.f32<=1;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t dimensions[]={1,1};float alpha=0.5f;bool absolute=false;
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=0;while(p<4 && strcmp(key,params[p].id)) ++p;
        if (p==4) return ORPHEUS_ERR_INVALID_ARG;
        if (p<2) {
            if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<1 || value->value.i32>params[p].max_i32)
                return ORPHEUS_ERR_INVALID_ARG;
            dimensions[p]=(uint32_t)value->value.i32;
        } else if (p==2) {
            if (!alpha_valid(value)) return ORPHEUS_ERR_INVALID_ARG;
            alpha=value->value.f32;
        } else {
            if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
            absolute=value->value.b;
        }
    }
    float* history=(float*)calloc((size_t)dimensions[0]*dimensions[1],sizeof(float));
    if (!history) return ORPHEUS_ERR_OUT_OF_MEMORY;
    BulkOnePoleState* s=(BulkOnePoleState*)state;free(s->history);s->history=history;
    s->channels=dimensions[0];s->elements=dimensions[1];s->alpha=alpha;s->absolute_input=absolute;return ORPHEUS_OK;
}
static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    BulkOnePoleState* s=(BulkOnePoleState*)state;
    if (s->history) memset(s->history,0,(size_t)s->channels*s->elements*sizeof(float));
    return ORPHEUS_OK;
}
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    BulkOnePoleState* s=(BulkOnePoleState*)state;
    if (!strcmp(key,"alpha")) {
        if (!alpha_valid(value)) return ORPHEUS_ERR_INVALID_ARG;
        s->alpha=value->value.f32;
    } else if (!strcmp(key,"absolute_input")) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        s->absolute_input=value->value.b;
    } else return ORPHEUS_ERR_UNSUPPORTED;
    return ORPHEUS_OK;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const BulkOnePoleState* s=(const BulkOnePoleState*)state;
    if (!strcmp(key,"alpha")) {value->type=ORPHEUS_VALUE_FLOAT;value->value.f32=s->alpha;}
    else if (!strcmp(key,"absolute_input")) {value->type=ORPHEUS_VALUE_BOOL;value->value.b=s->absolute_input;}
    else if (!strcmp(key,"channels")) {value->type=ORPHEUS_VALUE_INT;value->value.i32=(int32_t)s->channels;}
    else if (!strcmp(key,"elements")) {value->type=ORPHEUS_VALUE_INT;value->value.i32=(int32_t)s->elements;}
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned frames,OrpheusSampleFormat format,bool input) {
    return b && b->data && b->channels==channels && b->interleaved && b->format==format &&
        b->frame_capacity>=frames && (!input || b->frame_count==frames);
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y ? x-y<nb : y-x<na;
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs || context->input_count<1 || context->output_count<1)
        return ORPHEUS_ERR_INVALID_ARG;
    BulkOnePoleState* s=(BulkOnePoleState*)state;if (!s->history) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    const OrpheusBuffer* in=context->inputs[0];const OrpheusBuffer* update=context->input_count>1 ? context->inputs[1] : NULL;
    OrpheusBuffer* out=context->outputs[0];size_t count=(size_t)s->channels*s->elements,bytes=count*sizeof(float);
    if (!valid(in,s->channels,s->elements,ORPHEUS_FORMAT_F32,true) ||
        (update && !valid(update,1,1,ORPHEUS_FORMAT_I32,true))) return ORPHEUS_ERR_INVALID_ARG;
    if (out && (!valid(out,s->channels,s->elements,ORPHEUS_FORMAT_F32,false) || overlap(in->data,bytes,out->data,bytes) ||
        (update && overlap(update->data,sizeof(int32_t),out->data,bytes)))) return ORPHEUS_ERR_INVALID_ARG;
    if (!update || *(const int32_t*)update->data) {
        const float* input=(const float*)in->data;float factor=1.0f-s->alpha;
        for (size_t i=0;i<count;++i) {
            float value=s->absolute_input ? fabsf(input[i]) : input[i];
            volatile float feed=factor*value,feedback=s->history[i]*s->alpha;
            s->history[i]=feed+feedback;
        }
    }
    if (out) {memcpy(out->data,s->history,bytes);out->frame_count=s->elements;}
    return orpheus_process_next(context);
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,.process=process,
    .set_parameter=set_parameter,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
