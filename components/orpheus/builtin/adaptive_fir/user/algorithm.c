#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_adaptive_fir_prepare(orpheus_builtin_adaptive_fir_State* s,
    const OrpheusConfig* config) {
    (void)config;
    size_t count=(size_t)s->channels*s->filter_length;
    s->algorithm.w=calloc(count,sizeof(float));
    s->algorithm.xbuf=calloc(count,sizeof(float));
    s->algorithm.dbuf=calloc(count,sizeof(float));
    s->algorithm.pos=calloc((size_t)s->channels,sizeof(uint32_t));
    return s->algorithm.w && s->algorithm.xbuf && s->algorithm.dbuf && s->algorithm.pos
        ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

int orpheus_builtin_adaptive_fir_destroy(orpheus_builtin_adaptive_fir_State* s) {
    free(s->algorithm.w);free(s->algorithm.xbuf);free(s->algorithm.dbuf);free(s->algorithm.pos);
    s->algorithm.w=NULL;s->algorithm.xbuf=NULL;s->algorithm.dbuf=NULL;s->algorithm.pos=NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_adaptive_fir_reset(orpheus_builtin_adaptive_fir_State* s) {
    if (!s->algorithm.w || !s->algorithm.xbuf || !s->algorithm.dbuf || !s->algorithm.pos)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)s->channels*s->filter_length*sizeof(float);
    memset(s->algorithm.w,0,bytes);memset(s->algorithm.xbuf,0,bytes);memset(s->algorithm.dbuf,0,bytes);
    memset(s->algorithm.pos,0,(size_t)s->channels*sizeof(uint32_t));
    s->algorithm.total_frames=0;s->conv_metric=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_adaptive_fir_get_parameter(orpheus_builtin_adaptive_fir_State* s,
    const char* key, OrpheusValue* value) {
    if (strcmp(key,"detail")) return ORPHEUS_ERR_NOT_FOUND;
    if (!isfinite(s->conv_metric)) return ORPHEUS_ERR_INVALID_ARG;
    int length=snprintf(s->algorithm.json_detail,sizeof(s->algorithm.json_detail),
        "{\"filter_length\":%u,\"conv_metric\":%.6g}",(unsigned)s->filter_length,(double)s->conv_metric);
    if (length<0 || (size_t)length>=sizeof(s->algorithm.json_detail)) return ORPHEUS_ERR_PROCESSING;
    value->type=ORPHEUS_VALUE_STRING;value->value.str=s->algorithm.json_detail;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

static bool private_overlap(const orpheus_builtin_adaptive_fir_State* s, const void* data, size_t bytes) {
    size_t history_bytes=(size_t)s->channels*s->filter_length*sizeof(float);
    return overlap(data,bytes,s,sizeof(*s)) || overlap(data,bytes,s->algorithm.w,history_bytes) ||
        overlap(data,bytes,s->algorithm.xbuf,history_bytes) ||
        overlap(data,bytes,s->algorithm.dbuf,history_bytes) ||
        overlap(data,bytes,s->algorithm.pos,(size_t)s->channels*sizeof(uint32_t));
}

int orpheus_builtin_adaptive_fir_algorithm(orpheus_builtin_adaptive_fir_State* s,
    orpheus_builtin_adaptive_fir_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!s->algorithm.w || !s->algorithm.xbuf || !s->algorithm.dbuf || !s->algorithm.pos ||
        !io->x.buffer || !io->deriv.buffer || !io->err.buffer || !io->out.buffer ||
        io->x.buffer->frame_count<ctx->frame_count || io->deriv.buffer->frame_count<ctx->frame_count ||
        io->err.buffer->frame_count<ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if (private_overlap(s,io->x.samples,bytes) || private_overlap(s,io->deriv.samples,bytes) ||
        private_overlap(s,io->err.samples,bytes) || private_overlap(s,io->out.samples,bytes) ||
        overlap(io->x.samples,bytes,io->out.samples,bytes) ||
        overlap(io->deriv.samples,bytes,io->out.samples,bytes) ||
        overlap(io->err.samples,bytes,io->out.samples,bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t channels=(uint32_t)s->channels,length=(uint32_t)s->filter_length;
    double pe=0;
    for (uint32_t c=0;c<channels;++c) {
        float* w=s->algorithm.w+(size_t)c*length;
        float* x=s->algorithm.xbuf+(size_t)c*length;
        float* deriv=s->algorithm.dbuf+(size_t)c*length;
        uint32_t p=s->algorithm.pos[c];
        for (uint32_t n=0;n<ctx->frame_count;++n) {
            x[p]=io->x.samples[n*channels+c];
            deriv[p]=io->deriv.samples[n*channels+c];
            float y=0,dnorm=0;
            for (uint32_t k=0;k<length;++k) {
                uint32_t index=(p+length-k)%length;
                y+=w[k]*x[index];
                dnorm+=deriv[index]*deriv[index];
            }
            float error=io->err.samples[n*channels+c]-s->secondary_gain*y;
            float scale=s->step_size*error/(dnorm+s->eps);
            for (uint32_t k=0;k<length;++k) {
                uint32_t index=(p+length-k)%length;
                w[k]=s->leakage*w[k]+scale*deriv[index];
            }
            io->out.samples[n*channels+c]=y;
            pe+=(double)(error*error);
            p=(p+1)%length;
        }
        s->algorithm.pos[c]=p;
    }
    s->algorithm.total_frames++;
    s->conv_metric=(float)sqrt(pe/((double)ctx->frame_count*channels+1e-12));
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_adaptive_fir_emit_out(ctx,&io->out);
}
