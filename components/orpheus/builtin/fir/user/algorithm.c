#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse(const char* text, float* output, uint32_t* count) {
    *count=0;
    while (isspace((unsigned char)*text)) ++text;
    if (!*text) {
        if (output) output[0]=1.0f;
        *count=1;
        return ORPHEUS_OK;
    }
    for (unsigned i=0;;++i) {
        if (i==1024) return ORPHEUS_ERR_INVALID_ARG;
        char* end;
        errno=0;
        double value=strtod(text,&end);
        if (end==text || errno==ERANGE || !isfinite(value) || fabs(value)>FLT_MAX)
            return ORPHEUS_ERR_INVALID_ARG;
        if (output) output[i]=(float)value;
        *count=i+1;
        bool whitespace=isspace((unsigned char)*end)!=0;
        while (isspace((unsigned char)*end)) ++end;
        if (!*end) return ORPHEUS_OK;
        if (*end==',') {
            text=end+1;
            while (isspace((unsigned char)*text)) ++text;
            if (!*text) return ORPHEUS_ERR_INVALID_ARG;
        } else if (whitespace) text=end;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
}

int orpheus_builtin_fir_validate(const orpheus_builtin_fir_Parameters* p) {
    uint32_t count;
    return parse(p->coefficients,NULL,&count);
}

int orpheus_builtin_fir_prepare(orpheus_builtin_fir_State* s, const OrpheusConfig* config) {
    (void)config;
    float values[1024];uint32_t count;
    int rc=parse(s->coefficients,values,&count);
    if (rc) return rc;
    s->algorithm.coeffs=malloc(count*sizeof(float));
    s->algorithm.delay=calloc((size_t)count*s->channels,sizeof(float));
    s->algorithm.pos=calloc((size_t)s->channels,sizeof(uint32_t));
    if (!s->algorithm.coeffs || !s->algorithm.delay || !s->algorithm.pos) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memcpy(s->algorithm.coeffs,values,count*sizeof(float));
    s->taps=(int32_t)count;
    return ORPHEUS_OK;
}

int orpheus_builtin_fir_destroy(orpheus_builtin_fir_State* s) {
    free(s->algorithm.coeffs);free(s->algorithm.delay);free(s->algorithm.pos);
    s->algorithm.coeffs=NULL;s->algorithm.delay=NULL;s->algorithm.pos=NULL;
    s->taps=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_fir_reset(orpheus_builtin_fir_State* s) {
    if (s->algorithm.delay)
        memset(s->algorithm.delay,0,(size_t)s->taps*s->channels*sizeof(float));
    if (s->algorithm.pos) memset(s->algorithm.pos,0,(size_t)s->channels*sizeof(uint32_t));
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_fir_algorithm(orpheus_builtin_fir_State* s,
    orpheus_builtin_fir_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count ||
        !s->algorithm.coeffs || !s->algorithm.delay || !s->algorithm.pos || s->taps<1)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    size_t delay_bytes=(size_t)s->taps*s->channels*sizeof(float);
    if ((io->in.samples!=io->out.samples && overlap(io->in.samples,bytes,io->out.samples,bytes)) ||
        overlap(io->in.samples,bytes,s->algorithm.delay,delay_bytes) ||
        overlap(io->out.samples,bytes,s->algorithm.delay,delay_bytes) ||
        overlap(io->out.samples,bytes,s->algorithm.coeffs,(size_t)s->taps*sizeof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t channels=(uint32_t)s->channels,taps=(uint32_t)s->taps;
    for (uint32_t n=0;n<ctx->frame_count;++n) {
        for (uint32_t c=0;c<channels;++c) {
            uint32_t pos=s->algorithm.pos[c];
            s->algorithm.delay[pos*channels+c]=io->in.samples[(size_t)n*channels+c];
            float acc=0.0f;
            for (uint32_t k=0;k<taps;++k) {
                uint32_t index=(pos+taps-k)%taps;
                acc+=s->algorithm.coeffs[k]*s->algorithm.delay[index*channels+c];
            }
            io->out.samples[(size_t)n*channels+c]=acc;
            s->algorithm.pos[c]=(pos+1)%taps;
        }
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_fir_emit_out(ctx,&io->out);
}
