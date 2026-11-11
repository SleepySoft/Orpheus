#include "component_state.h"
#include "component_api.h"
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_nlms_prepare(orpheus_builtin_nlms_State* s, const OrpheusConfig* config) {
    (void)config;
    size_t count=(size_t)s->channels*s->filter_length;
    s->algorithm.w=calloc(count,sizeof(float));
    s->algorithm.x=calloc(count,sizeof(float));
    s->algorithm.pos=calloc((size_t)s->channels,sizeof(uint32_t));
    return s->algorithm.w && s->algorithm.x && s->algorithm.pos ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

int orpheus_builtin_nlms_destroy(orpheus_builtin_nlms_State* s) {
    free(s->algorithm.w);free(s->algorithm.x);free(s->algorithm.pos);
    s->algorithm.w=NULL;s->algorithm.x=NULL;s->algorithm.pos=NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_nlms_reset(orpheus_builtin_nlms_State* s) {
    if (!s->algorithm.w || !s->algorithm.x || !s->algorithm.pos) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)s->channels*s->filter_length*sizeof(float);
    memset(s->algorithm.w,0,bytes);memset(s->algorithm.x,0,bytes);
    memset(s->algorithm.pos,0,(size_t)s->channels*sizeof(uint32_t));
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

static bool private_overlap(const orpheus_builtin_nlms_State* s, const void* data, size_t bytes) {
    size_t history_bytes=(size_t)s->channels*s->filter_length*sizeof(float);
    return overlap(data,bytes,s,sizeof(*s)) || overlap(data,bytes,s->algorithm.w,history_bytes) ||
        overlap(data,bytes,s->algorithm.x,history_bytes) ||
        overlap(data,bytes,s->algorithm.pos,(size_t)s->channels*sizeof(uint32_t));
}

int orpheus_builtin_nlms_algorithm(orpheus_builtin_nlms_State* s,
    orpheus_builtin_nlms_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!s->algorithm.w || !s->algorithm.x || !s->algorithm.pos || !io->ref.buffer || !io->out.buffer ||
        io->ref.buffer->frame_count<ctx->frame_count ||
        (io->err.buffer && io->err.buffer->frame_count<ctx->frame_count))
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if (private_overlap(s,io->ref.samples,bytes) || private_overlap(s,io->out.samples,bytes) ||
        overlap(io->ref.samples,bytes,io->out.samples,bytes) ||
        (io->err.buffer && (private_overlap(s,io->err.samples,bytes) ||
         overlap(io->err.samples,bytes,io->out.samples,bytes))))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t channels=(uint32_t)s->channels,length=(uint32_t)s->filter_length;
    float mu=s->step_size,leak=s->leakage,eps=s->eps;
    for (uint32_t c=0;c<channels;++c) {
        float* w=s->algorithm.w+(size_t)c*length;
        float* x=s->algorithm.x+(size_t)c*length;
        uint32_t p=s->algorithm.pos[c];
        for (uint32_t n=0;n<ctx->frame_count;++n) {
            x[p]=io->ref.samples[n*channels+c];
            float y=0.0f,norm=0.0f;
            for (uint32_t i=0;i<length;++i) {
                uint32_t index=(p+length-i)%length;
                float value=x[index];
                y+=w[i]*value;
                norm+=value*value;
            }
            io->out.samples[n*channels+c]=y;
            if (io->err.buffer) {
                float error=io->err.samples[n*channels+c];
                float scale=mu*error/(norm+eps);
                for (uint32_t i=0;i<length;++i) {
                    uint32_t index=(p+length-i)%length;
                    w[i]=leak*w[i]+scale*x[index];
                }
            }
            p=(p+1)%length;
        }
        s->algorithm.pos[c]=p;
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_nlms_emit_out(ctx,&io->out);
}
