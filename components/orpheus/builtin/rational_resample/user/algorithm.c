#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_rational_resample_normalize(orpheus_builtin_rational_resample_Parameters* p,const char* key) {
    (void)key;
    if(p->input_frames<1 || p->up<1 || p->down<1) return ORPHEUS_ERR_INVALID_ARG;
    uint64_t count=(uint64_t)p->input_frames*(uint32_t)p->up;
    if(count%(uint32_t)p->down || !(count/(uint32_t)p->down) ||
       count/(uint32_t)p->down>UINT32_MAX ||
       count/(uint32_t)p->down*(uint32_t)p->channels>SIZE_MAX/sizeof(float) ||
       (uint64_t)p->up*(uint32_t)p->taps>SIZE_MAX/sizeof(float) ||
       (uint64_t)p->channels*(uint32_t)p->taps>SIZE_MAX/sizeof(float))
        return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
int orpheus_builtin_rational_resample_reset(orpheus_builtin_rational_resample_State* s) {
    if(s->algorithm.history)
        memset(s->algorithm.history,0,(size_t)s->taps*s->channels*sizeof(float));
    s->algorithm.cursor=0;
    return ORPHEUS_OK;
}
int orpheus_builtin_rational_resample_destroy(orpheus_builtin_rational_resample_State* s) {
    free(s->algorithm.coefficients);free(s->algorithm.history);
    s->algorithm.coefficients=NULL;s->algorithm.history=NULL;
    return ORPHEUS_OK;
}
int orpheus_builtin_rational_resample_prepare(orpheus_builtin_rational_resample_State* s,const OrpheusConfig* config) {
    (void)config;
    s->algorithm.coefficients=calloc((size_t)s->up*s->taps,sizeof(float));
    s->algorithm.history=calloc((size_t)s->channels*s->taps,sizeof(float));
    if(!s->algorithm.coefficients || !s->algorithm.history) return ORPHEUS_ERR_OUT_OF_MEMORY;
    const double pi=3.14159265358979323846;
    const double cutoff=0.45*fmin(1.0,(double)s->up/s->down);
    const double delay=0.5*(s->taps-1);
    for(int32_t phase=0;phase<s->up;++phase) {
        double sum=0.0;
        for(int32_t tap=0;tap<s->taps;++tap) {
            const double x=tap-delay+(double)phase/s->up;
            const double angle=2.0*pi*cutoff*x;
            const double sinc=fabs(angle)<1e-12 ? 1.0 : sin(angle)/angle;
            const double w=2.0*pi*tap/(s->taps-1);
            const double window=0.42-0.5*cos(w)+0.08*cos(2.0*w);
            const double value=2.0*cutoff*sinc*window;
            s->algorithm.coefficients[(size_t)phase*s->taps+tap]=(float)value;
            sum+=value;
        }
        for(int32_t tap=0;tap<s->taps;++tap)
            s->algorithm.coefficients[(size_t)phase*s->taps+tap]/=(float)sum;
    }
    return orpheus_builtin_rational_resample_reset(s);
}
int orpheus_builtin_rational_resample_set_parameter(orpheus_builtin_rational_resample_State* s,
    const char* key,const OrpheusValue* value) {
    (void)s;(void)key;(void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}
static uint32_t gcd(uint32_t a,uint32_t b) {
    while(b) {uint32_t next=a%b;a=b;b=next;}return a;
}
static void push(orpheus_builtin_rational_resample_State* s,const float* input) {
    memcpy(s->algorithm.history+(size_t)s->algorithm.cursor*s->channels,input,
        (size_t)s->channels*sizeof(float));
    s->algorithm.cursor=(s->algorithm.cursor+1)%(uint32_t)s->taps;
}
int orpheus_builtin_rational_resample_algorithm(orpheus_builtin_rational_resample_State* s,
    orpheus_builtin_rational_resample_IO* io,const OrpheusProcessContext* ctx) {
    if(!ctx->frame_count) return ORPHEUS_OK;
    if(!s->algorithm.history || !s->algorithm.coefficients) return ORPHEUS_ERR_PROCESSING;
    uint32_t frames=(uint32_t)s->input_frames;
    uint32_t output_frames=(uint32_t)((uint64_t)frames*s->up/s->down);
    size_t input_bytes=(size_t)frames*s->channels*sizeof(float);
    size_t output_bytes=(size_t)output_frames*s->channels*sizeof(float);
    size_t history_bytes=(size_t)s->taps*s->channels*sizeof(float);
    size_t coefficient_bytes=(size_t)s->taps*s->up*sizeof(float);
    if(ctx->trigger_input!=io->in.input_index || !io->in.buffer ||
       io->in.buffer->frame_count!=frames || !io->out.buffer || !io->in.samples || !io->out.samples ||
       overlap(io->in.samples,input_bytes,io->out.samples,output_bytes) ||
       overlap(io->in.samples,input_bytes,s,sizeof(*s)) || overlap(io->out.samples,output_bytes,s,sizeof(*s)) ||
       overlap(io->in.samples,input_bytes,s->algorithm.history,history_bytes) ||
       overlap(io->out.samples,output_bytes,s->algorithm.history,history_bytes) ||
       overlap(io->in.samples,input_bytes,s->algorithm.coefficients,coefficient_bytes) ||
       overlap(io->out.samples,output_bytes,s->algorithm.coefficients,coefficient_bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t common=gcd((uint32_t)s->up,(uint32_t)s->down);
    uint32_t up=(uint32_t)s->up/common,down=(uint32_t)s->down/common;
    if(ctx->frame_index%down || ctx->frame_index>UINT64_MAX-frames ||
       ctx->frame_index/down>UINT64_MAX/up) return ORPHEUS_ERR_INVALID_ARG;
    uint64_t start=ctx->frame_index/down*up;
    if(start>UINT64_MAX-output_frames) return ORPHEUS_ERR_INVALID_ARG;
    if(s->up==s->down) {
        memcpy(io->out.samples,io->in.samples,output_bytes);
    } else {
        uint32_t consumed=0;
        for(uint32_t f=0;f<output_frames;++f) {
            const uint64_t position=(uint64_t)f*s->down;
            const uint32_t sample=(uint32_t)(position/(uint32_t)s->up);
            const uint32_t phase=(uint32_t)(position%(uint32_t)s->up);
            while(consumed<=sample) push(s,io->in.samples+(size_t)consumed++*s->channels);
            for(int32_t ch=0;ch<s->channels;++ch) {
                double value=0.0;
                for(int32_t tap=0;tap<s->taps;++tap) {
                    const uint32_t slot=(s->algorithm.cursor+(uint32_t)s->taps-1-(uint32_t)tap)%(uint32_t)s->taps;
                    value+=s->algorithm.history[(size_t)slot*s->channels+ch]*
                        (double)s->algorithm.coefficients[(size_t)phase*s->taps+tap];
                }
                io->out.samples[(size_t)f*s->channels+ch]=(float)value;
            }
        }
        while(consumed<frames) push(s,io->in.samples+(size_t)consumed++*s->channels);
    }
    io->out.delivery.frame_index=start;
    return orpheus_builtin_rational_resample_emit_out(ctx,&io->out);
}
