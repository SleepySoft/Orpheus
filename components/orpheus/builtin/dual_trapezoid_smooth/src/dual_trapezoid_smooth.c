#include "orpheus_dual_trapezoid_smooth.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
#define INTEGER(key,label,initial,high) \
    {.id=key,.name=label,.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=initial}, \
     .min_i32=1,.max_i32=high,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
    INTEGER("channels","通道数",1,64),INTEGER("bins","每批点数",129,65536),INTEGER("batches","顺序批次数",1,64),
#undef INTEGER
#define ALPHA(key,label,initial) \
    {.id=key,.name=label,.type=ORPHEUS_VALUE_FLOAT,.default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=initial}, \
     .min_f32=0,.max_f32=1,.update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY}
    ALPHA("fast_alpha","第一历史系数",0.5f),ALPHA("slow_alpha","第二历史系数",0.95f)
#undef ALPHA
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="fast",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="slow",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.dual_trapezoid_smooth",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=3,.params=params,.param_count=5,.state_size=sizeof(DualTrapezoidSmoothState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    if (config && config->state_block) {
        *state=config->state_block;memset(*state,0,sizeof(DualTrapezoidSmoothState));
    } else {
        *state=calloc(1,sizeof(DualTrapezoidSmoothState));
        if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
        ((DualTrapezoidSmoothState*)*state)->owns_memory=true;
    }
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (!state) return ORPHEUS_OK;
    DualTrapezoidSmoothState* s=(DualTrapezoidSmoothState*)state;
    free(s->history);s->history=NULL;
    if (s->owns_memory) free(state);
    return ORPHEUS_OK;
}
static bool alpha_valid(const OrpheusValue* value) {
    return value && value->type==ORPHEUS_VALUE_FLOAT && isfinite(value->value.f32) &&
        value->value.f32>=0 && value->value.f32<=1;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t dimensions[]={1,129,1};float alphas[]={0.5f,0.95f};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=0;while(p<5 && strcmp(key,params[p].id)) ++p;
        if (p==5) return ORPHEUS_ERR_INVALID_ARG;
        if (p<3) {
            if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<1 || value->value.i32>params[p].max_i32)
                return ORPHEUS_ERR_INVALID_ARG;
            dimensions[p]=(uint32_t)value->value.i32;
        } else {
            if (!alpha_valid(value)) return ORPHEUS_ERR_INVALID_ARG;
            alphas[p-3]=value->value.f32;
        }
    }
    float* history=(float*)calloc((size_t)dimensions[0]*dimensions[1]*3,sizeof(float));
    if (!history) return ORPHEUS_ERR_OUT_OF_MEMORY;
    DualTrapezoidSmoothState* s=(DualTrapezoidSmoothState*)state;
    free(s->history);s->history=history;
    s->channels=dimensions[0];s->bins=dimensions[1];s->batches=dimensions[2];
    s->fast_alpha=alphas[0];s->slow_alpha=alphas[1];
    return ORPHEUS_OK;
}
static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    DualTrapezoidSmoothState* s=(DualTrapezoidSmoothState*)state;
    if (s->history) memset(s->history,0,(size_t)s->channels*s->bins*3*sizeof(float));
    return ORPHEUS_OK;
}
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    if (!state || !key || !alpha_valid(value)) return ORPHEUS_ERR_INVALID_ARG;
    DualTrapezoidSmoothState* s=(DualTrapezoidSmoothState*)state;
    if (!strcmp(key,"fast_alpha")) s->fast_alpha=value->value.f32;
    else if (!strcmp(key,"slow_alpha")) s->slow_alpha=value->value.f32;
    else return ORPHEUS_ERR_UNSUPPORTED;
    return ORPHEUS_OK;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const DualTrapezoidSmoothState* s=(const DualTrapezoidSmoothState*)state;
    if (!strcmp(key,"fast_alpha") || !strcmp(key,"slow_alpha")) {
        value->type=ORPHEUS_VALUE_FLOAT;
        value->value.f32=!strcmp(key,"fast_alpha") ? s->fast_alpha : s->slow_alpha;
    } else {
        value->type=ORPHEUS_VALUE_INT;
        if (!strcmp(key,"channels")) value->value.i32=s->channels;
        else if (!strcmp(key,"bins")) value->value.i32=s->bins;
        else if (!strcmp(key,"batches")) value->value.i32=s->batches;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
    return ORPHEUS_OK;
}
static bool overlap(const void* a,const void* b,size_t bytes) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y ? x-y<bytes : y-x<bytes;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned frames,bool input) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->channels==channels && b->frame_capacity>=frames && (!input || b->frame_count==frames);
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        context->input_count<1 || context->output_count<2) return ORPHEUS_ERR_INVALID_ARG;
    DualTrapezoidSmoothState* s=(DualTrapezoidSmoothState*)state;
    if (!s->history || !context->inputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return ORPHEUS_OK;
    const unsigned frames=s->bins*s->batches;
    const size_t points=(size_t)s->bins*s->channels,bytes=points*s->batches*sizeof(float);
    const OrpheusBuffer* in=context->inputs[0];
    if (!valid(in,s->channels,frames,true)) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned p=0;p<2;++p) {
        const OrpheusBuffer* out=context->outputs[p];
        if (!out) continue;
        if (!valid(out,s->channels,frames,false) || overlap(in->data,out->data,bytes) ||
            (p && context->outputs[0] && overlap(context->outputs[0]->data,out->data,bytes)))
            return ORPHEUS_ERR_INVALID_ARG;
    }
    const float* input=(const float*)in->data;
    float* previous=s->history;float* histories[]={previous+points,previous+points*2};
    const float alphas[]={s->fast_alpha,s->slow_alpha};
    const float factors[]={(1.0f-alphas[0])*0.5f,(1.0f-alphas[1])*0.5f};
    for (unsigned h=0;h<s->batches;++h) for (size_t i=0;i<points;++i) {
        size_t index=h*points+i;float current=input[index];
        float pair=current+previous[i];
        for (unsigned p=0;p<2;++p) {
            /* 梯形输入先相加再乘，保留两次乘法舍入，不融合。 */
            volatile float feed=pair*factors[p],feedback=histories[p][i]*alphas[p];
            float value=feed+feedback;histories[p][i]=value;
            if (context->outputs[p]) ((float*)context->outputs[p]->data)[index]=value;
        }
        previous[i]=current;
    }
    for (unsigned p=0;p<2;++p) if (context->outputs[p]) context->outputs[p]->frame_count=frames;
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.set_parameter=set_parameter,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
