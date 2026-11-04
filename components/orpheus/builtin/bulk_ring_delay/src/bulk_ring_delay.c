#include "orpheus_bulk_ring_delay.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusParameter params[]={
#define DIM(key,label,initial,high,signature) \
    {.id=key,.name=label,.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=initial}, \
     .min_i32=1,.max_i32=high,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=signature}
    DIM("channels","通道数",1,64,true),DIM("bins","每批行数",129,65536,true),
    DIM("batches","顺序批次数",1,64,true),DIM("capacity_elements","每通道容量",2193,1048576,false),
#undef DIM
    {.id="delay_elements",.name="读取偏移",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},.min_i32=0,.max_i32=INT32_MAX,
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY}
};
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.bulk_ring_delay",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=2,.params=params,.param_count=5,.state_size=sizeof(BulkRingDelayState),
    .alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) {return &descriptor;}
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    if (config && config->state_block) {
        *state=config->state_block;memset(*state,0,sizeof(BulkRingDelayState));
    } else {
        *state=calloc(1,sizeof(BulkRingDelayState));
        if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
        ((BulkRingDelayState*)*state)->owns_memory=true;
    }
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (!state) return ORPHEUS_OK;
    BulkRingDelayState* s=(BulkRingDelayState*)state;
    free(s->history);s->history=NULL;
    if (s->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    BulkRingDelayState* s=(BulkRingDelayState*)state;s->cursor=0;
    if (s->history) memset(s->history,0,(size_t)s->capacity_elements*s->channels*sizeof(float));
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t values[]={1,129,1,2193,0};
    for (unsigned i=0;i<config->param_count;++i) {
        const char* key=config->param_ids[i];const OrpheusValue* value=&config->param_values[i];
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=0;while(p<5 && strcmp(key,params[p].id)) ++p;
        if (p==5 || value->type!=ORPHEUS_VALUE_INT || value->value.i32<params[p].min_i32 ||
            value->value.i32>params[p].max_i32) return ORPHEUS_ERR_INVALID_ARG;
        values[p]=(uint32_t)value->value.i32;
    }
    if (values[3]<values[1]) return ORPHEUS_ERR_INVALID_ARG;
    float* history=(float*)calloc((size_t)values[3]*values[0],sizeof(float));
    if (!history) return ORPHEUS_ERR_OUT_OF_MEMORY;
    BulkRingDelayState* s=(BulkRingDelayState*)state;
    free(s->history);s->history=history;
    s->channels=values[0];s->bins=values[1];s->batches=values[2];
    s->capacity_elements=values[3];s->delay_elements=values[4];s->cursor=0;
    return ORPHEUS_OK;
}
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key,"delay_elements")) return ORPHEUS_ERR_UNSUPPORTED;
    if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<0) return ORPHEUS_ERR_INVALID_ARG;
    ((BulkRingDelayState*)state)->delay_elements=(uint32_t)value->value.i32;
    return ORPHEUS_OK;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const BulkRingDelayState* s=(const BulkRingDelayState*)state;value->type=ORPHEUS_VALUE_INT;
    if (!strcmp(key,"channels")) value->value.i32=s->channels;
    else if (!strcmp(key,"bins")) value->value.i32=s->bins;
    else if (!strcmp(key,"batches")) value->value.i32=s->batches;
    else if (!strcmp(key,"capacity_elements")) value->value.i32=s->capacity_elements;
    else if (!strcmp(key,"delay_elements")) value->value.i32=s->delay_elements;
    else return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static bool valid(const OrpheusBuffer* b,unsigned channels,unsigned frames,bool input) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->channels==channels && b->frame_capacity>=frames && (!input || b->frame_count==frames);
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        context->input_count<1 || context->output_count<1) return ORPHEUS_ERR_INVALID_ARG;
    BulkRingDelayState* s=(BulkRingDelayState*)state;
    if (!s->history || !context->inputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    const unsigned frames=s->bins*s->batches;
    const size_t row=(size_t)s->channels*sizeof(float),bytes=frames*row;
    const OrpheusBuffer* in=context->inputs[0];OrpheusBuffer* out=context->outputs[0];
    if (!valid(in,s->channels,frames,true)) return ORPHEUS_ERR_INVALID_ARG;
    if (out) {
        uintptr_t a=(uintptr_t)in->data,b=(uintptr_t)out->data;
        if (!valid(out,s->channels,frames,false) || (a>=b ? a-b<bytes : b-a<bytes))
            return ORPHEUS_ERR_INVALID_ARG;
    }
    for (unsigned h=0;h<s->batches;++h) {
        size_t first=s->capacity_elements-s->cursor;if(first>s->bins) first=s->bins;
        memcpy(s->history+(size_t)s->cursor*s->channels,(const float*)in->data+(size_t)h*s->bins*s->channels,first*row);
        memcpy(s->history,(const float*)in->data+((size_t)h*s->bins+first)*s->channels,(s->bins-first)*row);
        if (out) {
            unsigned read=(s->cursor+s->capacity_elements-s->delay_elements%s->capacity_elements)%s->capacity_elements;
            size_t head=s->capacity_elements-read;if(head>s->bins) head=s->bins;
            float* output=(float*)out->data+(size_t)h*s->bins*s->channels;
            memcpy(output,s->history+(size_t)read*s->channels,head*row);
            memcpy(output+head*s->channels,s->history,(s->bins-head)*row);
        }
        s->cursor=(s->cursor+s->bins)%s->capacity_elements;
    }
    if (out) out->frame_count=frames;
    return orpheus_process_next(context);
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.set_parameter=set_parameter,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {return &interface;}
