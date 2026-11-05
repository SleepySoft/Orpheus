#include "orpheus_bulk_math.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define PORT(ID,D) {.id=ID,.direction=D,.type=ORPHEUS_PORT_BULK,.sample_format=ORPHEUS_FORMAT_F32,.channels=1,.block_size=387}
static const OrpheusPort ports[]={PORT("a",ORPHEUS_PORT_INPUT),PORT("b",ORPHEUS_PORT_INPUT),PORT("out",ORPHEUS_PORT_OUTPUT)};
#undef PORT
#define IP(ID,V) {.id=ID,.name=ID,.type=ORPHEUS_VALUE_INT,.default_value={.type=ORPHEUS_VALUE_INT,.value.i32=V},.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED}
#define FP(ID) {.id=ID,.name=ID,.type=ORPHEUS_VALUE_FLOAT,.default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0},.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED}
static const OrpheusParameter params[]={IP("channels",1),IP("elements",387),IP("operation",2),FP("a"),FP("b")};
#undef IP
#undef FP
static const OrpheusComponentDescriptor descriptor={.id="orpheus.builtin.bulk_math",.version="1.0.0",.abi_version=ORPHEUS_ABI_VERSION,.ports=ports,.port_count=3,.params=params,.param_count=5,.state_size=sizeof(BulkMathState),.alignment=8,.realtime_safe=true};
static const OrpheusComponentDescriptor* get_descriptor(void){return &descriptor;}
static int create(void** state,const OrpheusConfig* config){
    if(!state)return ORPHEUS_ERR_INVALID_ARG;
    bool external=config&&config->state_block;*state=external?config->state_block:calloc(1,sizeof(BulkMathState));
    if(!*state)return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state,0,sizeof(BulkMathState));((BulkMathState*)*state)->owns_memory=!external;return 0;
}
static int destroy(void* s){if(s&&((BulkMathState*)s)->owns_memory)free(s);return 0;}
static int prepare(void* state,const OrpheusConfig* config){
    if(!state||!config||(config->param_count&&(!config->param_ids||!config->param_values)))return ORPHEUS_ERR_INVALID_ARG;
    BulkMathState s={.channels=1,.elements=387,.operation=2,.owns_memory=((BulkMathState*)state)->owns_memory};
    for(unsigned i=0;i<config->param_count;++i){
        const char* k=config->param_ids[i];const OrpheusValue* v=config->param_values+i;
        if(!k)return ORPHEUS_ERR_INVALID_ARG;
        if(!strcmp(k,"a")||!strcmp(k,"b")){
            if(v->type!=ORPHEUS_VALUE_FLOAT||!isfinite(v->value.f32))return ORPHEUS_ERR_INVALID_ARG;
            if(*k=='a')s.a=v->value.f32;else s.b=v->value.f32;
        }else{
            if(v->type!=ORPHEUS_VALUE_INT)return ORPHEUS_ERR_INVALID_ARG;
            if(!strcmp(k,"channels"))s.channels=v->value.i32;
            else if(!strcmp(k,"elements"))s.elements=v->value.i32;
            else if(!strcmp(k,"operation"))s.operation=v->value.i32;
            else return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    if(s.channels<1||s.channels>128||s.elements<1||s.elements>65536||s.operation<0||s.operation>4)return ORPHEUS_ERR_INVALID_ARG;
    *(BulkMathState*)state=s;return 0;
}
static int reset(void* s){return s?0:ORPHEUS_ERR_INVALID_ARG;}
static bool valid(const OrpheusBuffer* b,const BulkMathState* s,bool input){
    return b&&b->data&&b->interleaved&&b->format==ORPHEUS_FORMAT_F32&&b->channels==(unsigned)s->channels&&
        b->frame_capacity>=(unsigned)s->elements&&(!input||b->frame_count==(unsigned)s->elements);
}
static float magnitude(float a,float b){
    float x=fabsf(a),y=fabsf(b),r;
    if(x<y){r=x/y;return sqrtf(r*r+1.0F)*y;}
    if(x>y){r=y/x;return sqrtf(r*r+1.0F)*x;}
    return x*1.41421354F;
}
static int process(void* state,const OrpheusProcessContext* c){
    if(!state||!c||!c->inputs||!c->outputs||c->input_count<2||c->output_count<1)return ORPHEUS_ERR_INVALID_ARG;
    if(!c->frame_count)return 0;
    const BulkMathState* s=state;OrpheusBuffer* out=c->outputs[0];
    size_t count=(size_t)s->channels*s->elements,bytes=count*sizeof(float);
    if(out&&!valid(out,s,false))return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned j=0;j<2;++j)if(c->inputs[j]){
        if(!valid(c->inputs[j],s,true))return ORPHEUS_ERR_INVALID_ARG;
        if(out){uintptr_t a=(uintptr_t)c->inputs[j]->data,b=(uintptr_t)out->data;if((a>b?a-b:b-a)<bytes)return ORPHEUS_ERR_INVALID_ARG;}
    }
    if(out){
        const float* a=c->inputs[0]?c->inputs[0]->data:NULL;const float* b=c->inputs[1]?c->inputs[1]->data:NULL;float* y=out->data;
        for(size_t i=0;i<count;++i){float x=a?a[i]:s->a,z=b?b[i]:s->b;
            switch(s->operation){case 0:y[i]=x+z;break;case 1:y[i]=x-z;break;case 2:y[i]=x*z;break;case 3:y[i]=x/z;break;default:y[i]=magnitude(x,z);break;}
        }
        out->frame_count=(uint32_t)s->elements;
    }
    return orpheus_process_next(c);
}
static const OrpheusComponentInterface api={.get_descriptor=get_descriptor,.create=create,.destroy=destroy,.prepare=prepare,.reset=reset,.process=process};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void){return &api;}
