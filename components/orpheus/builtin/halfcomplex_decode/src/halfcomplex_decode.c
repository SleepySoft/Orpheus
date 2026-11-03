#include "orpheus_halfcomplex_decode.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
#define PARAM(key,label,initial,low,high) \
    {.id=key,.name=label,.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=initial}, \
     .min_i32=low,.max_i32=high,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.affects_signature=true}
    PARAM("channels","通道数",2,1,64),
    PARAM("fft_size","每帧半复数槽数",256,2,65536),
    PARAM("batches","批次数",1,1,64),
    PARAM("first_bin","首频点（从0起）",0,0,32768),
    PARAM("bins","输出频点数",129,1,32769)
#undef PARAM
};
static const OrpheusPort ports[] = {
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,
     .is_variable=true,.channels_param="channels"},
    {.id="spectrum",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,
     .is_variable=true,.channels_param="channels"},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,
     .is_variable=true,.channels_param="channels"},
    {.id="tick",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_I32,
     .channels=1}
};
static const OrpheusComponentDescriptor descriptor={
    .id="orpheus.builtin.halfcomplex_decode",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=4,.params=params,.param_count=5,
    .state_size=sizeof(HalfcomplexDecodeState),.alignment=8,.realtime_safe=true,.supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    if (config && config->state_block) {
        *state=config->state_block; memset(*state,0,sizeof(HalfcomplexDecodeState));
    } else {
        *state=calloc(1,sizeof(HalfcomplexDecodeState));
        if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
        ((HalfcomplexDecodeState*)*state)->owns_memory=true;
    }
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((HalfcomplexDecodeState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    int32_t values[]={2,256,1,0,129};
    for (unsigned i=0;i<config->param_count;++i) {
        if (!config->param_ids[i]) return ORPHEUS_ERR_INVALID_ARG;
        unsigned p=0;
        while (p<5 && strcmp(config->param_ids[i],params[p].id)) ++p;
        if (p==5 || config->param_values[i].type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        values[p]=config->param_values[i].value.i32;
    }
    for (unsigned p=0;p<5;++p)
        if (values[p]<params[p].min_i32 || values[p]>params[p].max_i32) return ORPHEUS_ERR_INVALID_ARG;
    if (values[1]%2 || values[3]+values[4]>values[1]/2+1) return ORPHEUS_ERR_INVALID_ARG;
    HalfcomplexDecodeState* s=(HalfcomplexDecodeState*)state;
    s->channels=values[0]; s->fft_size=values[1]; s->batches=values[2]; s->first_bin=values[3]; s->bins=values[4];
    return ORPHEUS_OK;
}
static int reset(void* state) { return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG; }
static int set_parameter(void* state,const char* key,const OrpheusValue* value) {
    (void)state; (void)key; (void)value; return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_parameter(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const HalfcomplexDecodeState* s=(const HalfcomplexDecodeState*)state;
    const uint32_t values[]={s->channels,s->fft_size,s->batches,s->first_bin,s->bins};
    for (unsigned p=0;p<5;++p) if (!strcmp(key,params[p].id)) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=(int32_t)values[p]; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_INVALID_ARG;
}
static bool overlap(const void* a,size_t a_bytes,const void* b,size_t b_bytes) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return (x>=y && x-y<b_bytes) || (y>=x && y-x<a_bytes);
}
static int process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        context->input_count<1 || context->output_count<1) return ORPHEUS_ERR_INVALID_ARG;
    const HalfcomplexDecodeState* s=(const HalfcomplexDecodeState*)state;
    const OrpheusBuffer* audio=context->inputs[0];
    const OrpheusBuffer* spectrum=context->input_count>1 ? context->inputs[1] : NULL;
    const OrpheusBuffer* in=audio ? audio : spectrum;
    OrpheusBuffer* out=context->outputs[0];
    OrpheusBuffer* tick=context->output_count>1 ? context->outputs[1] : NULL;
    if (!in || (audio && spectrum)) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return ORPHEUS_OK;
    const uint32_t input_frames=s->fft_size*s->batches,output_frames=s->bins*2*s->batches;
    if (!s->channels || !in->data || in->format!=ORPHEUS_FORMAT_F32 || !in->interleaved ||
        in->channels!=s->channels || in->frame_count!=input_frames || in->frame_capacity<input_frames)
        return ORPHEUS_ERR_INVALID_ARG;
    const size_t input_bytes=(size_t)input_frames*s->channels*sizeof(float);
    const size_t output_bytes=(size_t)output_frames*s->channels*sizeof(float);
    if (out && (!out->data || out->format!=ORPHEUS_FORMAT_F32 || !out->interleaved ||
        out->channels!=s->channels || out->frame_capacity<output_frames ||
        overlap(in->data,input_bytes,out->data,output_bytes))) return ORPHEUS_ERR_INVALID_ARG;
    if (tick && (!tick->data || tick->format!=ORPHEUS_FORMAT_I32 || !tick->interleaved ||
        tick->channels!=1 || tick->frame_capacity<1 ||
        overlap(in->data,input_bytes,tick->data,4) ||
        (out && overlap(out->data,output_bytes,tick->data,4)))) return ORPHEUS_ERR_INVALID_ARG;
    if (out) {
        const unsigned char* input=(const unsigned char*)in->data;
        unsigned char* output=(unsigned char*)out->data;
        const uint32_t zero=0;
        for (unsigned h=0;h<s->batches;++h) for (unsigned bin=0;bin<s->bins;++bin) {
            unsigned k=s->first_bin+bin;
            for (unsigned c=0;c<s->channels;++c) {
                size_t target=((size_t)h*s->bins*2+bin*2)*s->channels+c;
                size_t real=((size_t)h*s->fft_size+k)*s->channels+c;
                memcpy(output+target*4,input+real*4,4);
                if (k==0 || k==s->fft_size/2) memcpy(output+(target+s->channels)*4,&zero,4);
                else {
                    size_t imaginary=((size_t)h*s->fft_size+s->fft_size-k)*s->channels+c;
                    memcpy(output+(target+s->channels)*4,input+imaginary*4,4);
                }
            }
        }
        out->frame_count=output_frames;
    }
    if (tick) {
        const int32_t completed=1;
        memcpy(tick->data,&completed,4);
        tick->frame_count=1;
    }
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface interface={
    .get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,
    .process=process,.set_parameter=set_parameter,.get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
