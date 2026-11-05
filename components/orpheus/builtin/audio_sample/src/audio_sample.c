#include "orpheus_audio_sample.h"
#include <stdlib.h>
#include <string.h>
static const OrpheusPort ports[]={
    {.id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,.channels=2,.block_size=32},
    {.id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.channels=1,.block_size=1}
};
#define P(ID,V) {.id=ID,.name=ID,.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=V},.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED}
static const OrpheusParameter params[]={P("channels",2),P("frames",32),P("frame",0)};
#undef P
static const OrpheusComponentDescriptor descriptor={.id="orpheus.builtin.audio_sample",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,.ports=ports,.port_count=2,.params=params,.param_count=3,.state_size=sizeof(AudioSampleState),.alignment=8,.realtime_safe=true};
static const OrpheusComponentDescriptor* get_descriptor(void){return &descriptor;}
static int create(void** state,const OrpheusConfig* config){
    if(!state)return ORPHEUS_ERR_INVALID_ARG;
    bool external=config&&config->state_block;
    *state=external?config->state_block:calloc(1,sizeof(AudioSampleState));
    if(!*state)return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(AudioSampleState));((AudioSampleState*)*state)->owns_memory=!external;return 0;
}
static int destroy(void* state){if(state&&((AudioSampleState*)state)->owns_memory)free(state);return 0;}
static int prepare(void* state,const OrpheusConfig* config){
    if(!state||!config||(config->param_count&&(!config->param_ids||!config->param_values)))return ORPHEUS_ERR_INVALID_ARG;
    AudioSampleState next={.channels=2,.frames=32,.owns_memory=((AudioSampleState*)state)->owns_memory};
    for(unsigned i=0;i<config->param_count;++i){
        const char* key=config->param_ids[i];const OrpheusValue* v=config->param_values+i;
        if(!key||v->type!=ORPHEUS_VALUE_INT)return ORPHEUS_ERR_INVALID_ARG;
        if(!strcmp(key,"channels"))next.channels=v->value.i32;
        else if(!strcmp(key,"frames"))next.frames=v->value.i32;
        else if(!strcmp(key,"frame"))next.frame=v->value.i32;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
    if(next.channels<1||next.channels>32||next.frames<1||next.frames>65536||
       next.frame<0||next.frame>=next.frames)return ORPHEUS_ERR_INVALID_ARG;
    *(AudioSampleState*)state=next;return 0;
}
static int reset(void* state){return state?0:ORPHEUS_ERR_INVALID_ARG;}
static int process(void* state,const OrpheusProcessContext* c){
    if(!state||!c||!c->inputs||!c->outputs||c->input_count<1||c->output_count<1)return ORPHEUS_ERR_INVALID_ARG;
    if(!c->frame_count)return 0;
    const AudioSampleState* s=state;const OrpheusBuffer* in=c->inputs[0];
    if(c->output_count<(unsigned)s->channels)return ORPHEUS_ERR_INVALID_ARG;
    if(!in||!in->data||in->format!=ORPHEUS_FORMAT_F32||!in->interleaved||in->channels!=(unsigned)s->channels||
       in->frame_count!=(unsigned)s->frames||in->frame_capacity<(unsigned)s->frames)return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned ch=0;ch<(unsigned)s->channels;++ch){OrpheusBuffer* out=c->outputs[ch];if(out){
        if(!out->data||out->format!=ORPHEUS_FORMAT_F32||!out->interleaved||out->channels!=1||out->frame_capacity<1)return ORPHEUS_ERR_INVALID_ARG;
        uintptr_t a=(uintptr_t)in->data,b=(uintptr_t)out->data;
        if(b<a+(size_t)s->frames*s->channels*sizeof(float)&&a<b+sizeof(float))return ORPHEUS_ERR_INVALID_ARG;
        for(unsigned j=0;j<ch;++j)if(c->outputs[j]&&c->outputs[j]->data){
            uintptr_t other=(uintptr_t)c->outputs[j]->data;
            if((b>other?b-other:other-b)<sizeof(float))return ORPHEUS_ERR_INVALID_ARG;
        }
    }}
    for(unsigned ch=0;ch<(unsigned)s->channels;++ch){OrpheusBuffer* out=c->outputs[ch];if(out){
        memcpy(out->data,(const float*)in->data+s->frame*s->channels+ch,sizeof(float));out->frame_count=1;
    }}
    return orpheus_process_next(c);
}
static const OrpheusComponentInterface api={.get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,.process=process};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void){return &api;}
