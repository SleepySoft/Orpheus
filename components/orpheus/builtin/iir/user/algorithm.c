#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse(const char* text, uint32_t channels, uint32_t order, bool per_channel, float* output) {
    uint32_t width=2*order+1,count=(per_channel ? channels : 1)*width;
    for (uint32_t i=0;i<count;++i) {
        char* end;
        errno=0;
        float value=strtof(text,&end);
        if (end==text || errno==ERANGE || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        if (output) output[(i/width)*33+i%width]=value;
        while (isspace((unsigned char)*end)) ++end;
        if (i+1==count) return *end ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
        if (*end!=',') return ORPHEUS_ERR_INVALID_ARG;
        text=end+1;
    }
    return ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_iir_validate(const orpheus_builtin_iir_Parameters* p) {
    bool per_channel=!strcmp(p->coefs_mode,"per_channel");
    const char* text=per_channel && p->initial_coefs_per_channel[0] ? p->initial_coefs_per_channel : p->coefs;
    int rc=parse(text,(uint32_t)p->channels,(uint32_t)p->order,per_channel,NULL);
    if (!rc && !per_channel && p->initial_coefs_per_channel[0])
        rc=parse(p->initial_coefs_per_channel,(uint32_t)p->channels,(uint32_t)p->order,true,NULL);
    return rc;
}

int orpheus_builtin_iir_reset(orpheus_builtin_iir_State* s) {
    memset(s->algorithm.history,0,sizeof(s->algorithm.history));
    return ORPHEUS_OK;
}

int orpheus_builtin_iir_prepare(orpheus_builtin_iir_State* s, const OrpheusConfig* config) {
    (void)config;
    memset(s->algorithm.shared,0,sizeof(s->algorithm.shared));
    memset(s->algorithm.per_channel,0,sizeof(s->algorithm.per_channel));
    bool per_channel=!strcmp(s->coefs_mode,"per_channel");
    const char* text=per_channel && s->initial_coefs_per_channel[0] ? s->initial_coefs_per_channel : s->coefs;
    int rc=parse(text,(uint32_t)s->channels,(uint32_t)s->order,per_channel,
        per_channel ? s->algorithm.per_channel : s->algorithm.shared);
    if (!rc && !per_channel && s->initial_coefs_per_channel[0])
        rc=parse(s->initial_coefs_per_channel,(uint32_t)s->channels,(uint32_t)s->order,true,s->algorithm.per_channel);
    return rc ? rc : orpheus_builtin_iir_reset(s);
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_iir_algorithm(orpheus_builtin_iir_State* s,
    orpheus_builtin_iir_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if (overlap(io->out.samples,bytes,&s->algorithm,sizeof(s->algorithm)))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!s->processing_enabled) memset(io->out.samples,0,bytes);
    else {
        if (!io->in.buffer || io->in.buffer->frame_count<ctx->frame_count ||
            overlap(io->in.samples,bytes,io->out.samples,bytes) ||
            overlap(io->in.samples,bytes,s->algorithm.history,sizeof(s->algorithm.history)))
            return ORPHEUS_ERR_INVALID_ARG;
        bool per_channel=!strcmp(s->coefs_mode,"per_channel");
        for (uint32_t n=0;n<ctx->frame_count;++n) {
            for (int32_t c=0;c<s->channels;++c) {
                const float* coefs=per_channel ? s->algorithm.per_channel+c*33 : s->algorithm.shared;
                float* z=s->algorithm.history[c];
                float x=io->in.samples[(size_t)n*s->channels+c];
                float y=coefs[0]*x+z[0];
                for (int32_t k=0;k+1<s->order;++k)
                    z[k]=(z[k+1]+coefs[k+1]*x)-coefs[s->order+k+1]*y;
                z[s->order-1]=coefs[s->order]*x-coefs[2*s->order]*y;
                io->out.samples[(size_t)n*s->channels+c]=y;
            }
        }
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_iir_emit_out(ctx,&io->out);
}
