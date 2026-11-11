#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse(const char* text,uint32_t count,float* output) {
    if(output) memset(output,0,(size_t)count*sizeof(float));
    while(isspace((unsigned char)*text)) ++text;
    for(uint32_t i=0;*text;++i) {
        if(i==count) return ORPHEUS_ERR_INVALID_ARG;
        char* end;
        errno=0;
        float value=strtof(text,&end);
        if(end==text || !isfinite(value) || (errno==ERANGE && value==0))
            return ORPHEUS_ERR_INVALID_ARG;
        if(output) output[i]=value;
        bool whitespace=isspace((unsigned char)*end)!=0;
        while(isspace((unsigned char)*end)) ++end;
        if(!*end) return ORPHEUS_OK;
        if(*end==',') {
            text=end+1;
            while(isspace((unsigned char)*text)) ++text;
            if(!*text) return ORPHEUS_ERR_INVALID_ARG;
        } else if(whitespace) text=end;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_rnc_mimo_nlms_validate(const orpheus_builtin_rnc_mimo_nlms_Parameters* p) {
    int rc=parse(p->step_sizes,(uint32_t)p->output_channels,NULL);
    return rc ? rc : parse(p->initial_weights,
        (uint32_t)p->output_channels*p->reference_channels*p->filter_length,NULL);
}

int orpheus_builtin_rnc_mimo_nlms_reset(orpheus_builtin_rnc_mimo_nlms_State* s) {
    memcpy(s->algorithm.weights,s->algorithm.initial_weights,sizeof(s->algorithm.weights));
    memset(s->algorithm.history,0,sizeof(s->algorithm.history));
    s->algorithm.position=0;s->algorithm.leakage_reference=0;s->algorithm.leakage_output=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_rnc_mimo_nlms_prepare(orpheus_builtin_rnc_mimo_nlms_State* s,const OrpheusConfig* config) {
    (void)config;
    int rc=parse(s->step_sizes,(uint32_t)s->output_channels,s->algorithm.step_sizes);
    if(rc) return rc;
    rc=parse(s->initial_weights,(uint32_t)s->output_channels*s->reference_channels*s->filter_length,
        s->algorithm.initial_weights);
    return rc ? rc : orpheus_builtin_rnc_mimo_nlms_reset(s);
}

static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_rnc_mimo_nlms_algorithm(orpheus_builtin_rnc_mimo_nlms_State* s,
    orpheus_builtin_rnc_mimo_nlms_IO* io,const OrpheusProcessContext* ctx) {
    if(!ctx->frame_count) return ORPHEUS_OK;
    if(ctx->trigger_input!=io->ref.input_index || !io->ref.buffer || !io->filtered_error.buffer || !io->out.buffer ||
        io->ref.buffer->frame_count<ctx->frame_count || io->filtered_error.buffer->frame_count<ctx->frame_count ||
        s->algorithm.position>=(uint32_t)s->filter_length ||
        s->algorithm.leakage_reference>=(uint32_t)s->reference_channels ||
        s->algorithm.leakage_output>=(uint32_t)s->output_channels) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t refs=(uint32_t)s->reference_channels,outputs=(uint32_t)s->output_channels,taps=(uint32_t)s->filter_length;
    size_t input_bytes=(size_t)ctx->frame_count*refs*sizeof(float);
    size_t output_bytes=(size_t)ctx->frame_count*outputs*sizeof(float);
    if(overlap(io->ref.samples,input_bytes,s,sizeof(*s)) || overlap(io->filtered_error.samples,output_bytes,s,sizeof(*s)) ||
        overlap(io->out.samples,output_bytes,s,sizeof(*s)) ||
        overlap(io->ref.samples,input_bytes,io->out.samples,output_bytes) ||
        overlap(io->filtered_error.samples,output_bytes,io->out.samples,output_bytes)) return ORPHEUS_ERR_INVALID_ARG;
    if(s->leakage<1.0f) {
        size_t start=((size_t)s->algorithm.leakage_output*refs+s->algorithm.leakage_reference)*taps;
        for(uint32_t k=0;k<taps;k++) s->algorithm.weights[start+k]*=s->leakage;
        if(++s->algorithm.leakage_output==outputs) {
            s->algorithm.leakage_output=0;
            s->algorithm.leakage_reference=(s->algorithm.leakage_reference+1)%refs;
        }
    }
    for(uint32_t n=0;n<ctx->frame_count;n++) {
        float norm=s->eps;
        uint32_t p=s->algorithm.position;
        for(uint32_t r=0;r<refs;r++) {
            s->algorithm.history[r*taps+p]=io->ref.samples[(size_t)n*refs+r];
            for(uint32_t k=0;k<taps;k++) {
                float raw=s->algorithm.history[r*taps+(p+taps-k)%taps];
                norm+=raw*raw;
            }
        }
        for(uint32_t o=0;o<outputs;o++) {
            float value=0;
            float scale=s->algorithm.step_sizes[o]*io->filtered_error.samples[(size_t)n*outputs+o]/norm;
            for(uint32_t r=0;r<refs;r++) for(uint32_t k=0;k<taps;k++) {
                float raw=s->algorithm.history[r*taps+(p+taps-k)%taps];
                size_t index=((size_t)o*refs+r)*taps+k;
                value+=s->algorithm.weights[index]*raw;
                s->algorithm.weights[index]+=scale*raw;
            }
            io->out.samples[(size_t)n*outputs+o]=value;
        }
        s->algorithm.position=(p+1)%taps;
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_rnc_mimo_nlms_emit_out(ctx,&io->out);
}
