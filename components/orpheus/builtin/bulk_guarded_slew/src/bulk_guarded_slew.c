#include "orpheus_bulk_guarded_slew.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
    {.id="channels",.name="通道数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .min_i32=1,.max_i32=128,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
    {.id="elements",.name="每通道元素数",.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1},
     .min_i32=1,.max_i32=65536,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true},
#define FLOAT_PARAM(key,label,initial) \
    {.id=key,.name=label,.type=ORPHEUS_VALUE_FLOAT,.default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=initial}, \
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY}
    FLOAT_PARAM("attack","每次正向最大变化",1.0f),
    FLOAT_PARAM("decay","每次负向最小变化",-1.0f),
    FLOAT_PARAM("threshold","方向保持阈值",0.0f),
    FLOAT_PARAM("slow","边界慢速率幅值",0.0f),
    FLOAT_PARAM("initial","初始输出",0.0f)
#undef FLOAT_PARAM
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="bound",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.channels=1},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.bulk_guarded_slew",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .params=params,.param_count=7,.ports=ports,.port_count=3,.state_size=sizeof(BulkGuardedSlewState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=config && config->state_block ? config->state_block : calloc(1,sizeof(BulkGuardedSlewState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(BulkGuardedSlewState));
    ((BulkGuardedSlewState*)*state)->owns_memory=!(config && config->state_block);return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (!state) return ORPHEUS_OK;
    BulkGuardedSlewState* s=(BulkGuardedSlewState*)state;free(s->history);s->history=NULL;
    if (s->owns_memory) free(state);
    return ORPHEUS_OK;
}
static bool value_valid(const OrpheusValue* value,unsigned p) {
    if (!value || value->type!=ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32)) return false;
    float x=value->value.f32;
    return p==6 || (p==3 ? x<=0 : x>=0);
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t dimensions[]={1,1};float values[]={1,-1,0,0,0};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=config->param_values+i;
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=0;while(p<7 && strcmp(key,params[p].id)) ++p;
        if (p==7) return ORPHEUS_ERR_INVALID_ARG;
        if (p<2) {
            if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<1 || value->value.i32>params[p].max_i32)
                return ORPHEUS_ERR_INVALID_ARG;
            dimensions[p]=(uint32_t)value->value.i32;
        } else {
            if (!value_valid(value,p)) return ORPHEUS_ERR_INVALID_ARG;
            values[p-2]=value->value.f32;
        }
    }
    GuardedSlewHistory* history=(GuardedSlewHistory*)calloc((size_t)dimensions[0]*dimensions[1],sizeof(GuardedSlewHistory));
    if (!history) return ORPHEUS_ERR_OUT_OF_MEMORY;
    BulkGuardedSlewState* s=(BulkGuardedSlewState*)state;free(s->history);s->history=history;
    s->channels=dimensions[0];s->elements=dimensions[1];s->attack=values[0];s->decay=values[1];
    s->threshold=values[2];s->slow=values[3];s->initial=values[4];s->initialized=false;return ORPHEUS_OK;
}
static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    BulkGuardedSlewState* s=(BulkGuardedSlewState*)state;
    if (s->history) memset(s->history,0,(size_t)s->channels*s->elements*sizeof(GuardedSlewHistory));
    s->initialized=false;return ORPHEUS_OK;
}
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    unsigned p=0;while(p<7 && strcmp(key,params[p].id)) ++p;
    if (p==7) return ORPHEUS_ERR_INVALID_ARG;
    if (p<2) return ORPHEUS_ERR_UNSUPPORTED;
    if (!value_valid(value,p)) return ORPHEUS_ERR_INVALID_ARG;
    BulkGuardedSlewState* s=(BulkGuardedSlewState*)state;
    switch(p) {
        case 2:s->attack=value->value.f32;break;
        case 3:s->decay=value->value.f32;break;
        case 4:s->threshold=value->value.f32;break;
        case 5:s->slow=value->value.f32;break;
        case 6:s->initial=value->value.f32;break;
    }
    return ORPHEUS_OK;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    BulkGuardedSlewState* s=(BulkGuardedSlewState*)state;unsigned p=0;
    while(p<7 && strcmp(key,params[p].id)) ++p;
    if (p==7) return ORPHEUS_ERR_INVALID_ARG;
    if (p<2) {value->type=ORPHEUS_VALUE_INT;value->value.i32=(int32_t)(p==0?s->channels:s->elements);}
    else {
        const float values[]={s->attack,s->decay,s->threshold,s->slow,s->initial};
        value->type=ORPHEUS_VALUE_FLOAT;value->value.f32=values[p-2];
    }
    return ORPHEUS_OK;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned elements,bool input) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->channels==channels && b->interleaved &&
        b->frame_capacity>=elements && (!input || b->frame_count==elements);
}
static bool overlap(const void* a,size_t a_bytes,const void* b,size_t b_bytes) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return (x>=y && x-y<b_bytes) || (y>=x && y-x<a_bytes);
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        context->input_count<1 || context->output_count<1) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    BulkGuardedSlewState* s=(BulkGuardedSlewState*)state;
    const OrpheusBuffer* in=context->inputs[0];const OrpheusBuffer* bound=context->input_count>1?context->inputs[1]:NULL;
    OrpheusBuffer* out=context->outputs[0];size_t count=(size_t)s->channels*s->elements,bytes=count*4;
    if (!s->history || !s->channels || !s->elements || !valid(in,s->channels,s->elements,true) ||
        (bound && !valid(bound,1,1,true)) || (out && !valid(out,s->channels,s->elements,false))) return ORPHEUS_ERR_INVALID_ARG;
    if (out && (overlap(in->data,bytes,out->data,bytes) ||
        (bound && overlap(bound->data,4,out->data,bytes)))) return ORPHEUS_ERR_INVALID_ARG;
    float flag=0;if (bound) memcpy(&flag,bound->data,4);
    for (size_t i=0;i<count;++i) {
        GuardedSlewHistory* h=s->history+i;float x;memcpy(&x,(const unsigned char*)in->data+i*4,4);
        float previous=s->initialized?h->value:s->initial;
        float delta=x-previous;
        float change=delta>0?fminf(s->attack,delta):fmaxf(s->decay,delta);
        volatile float product=h->delta*delta;
        float slew=product>0?h->change:0.0f;
        if (flag!=0 && slew==0) slew=delta>0?fminf(s->slow,delta):fmaxf(-s->slow,delta);
        if (fabsf(delta)<=s->threshold && fabsf(slew)<fabsf(change)) change=slew;
        float next=previous+change;
        if (out) memcpy((unsigned char*)out->data+i*4,&next,4);
        h->value=next;h->delta=delta;h->change=change;
    }
    s->initialized=true;if (out) out->frame_count=s->elements;
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
