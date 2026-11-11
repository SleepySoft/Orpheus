#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_anc_fxlms_prepare(orpheus_builtin_anc_fxlms_State* s, const OrpheusConfig* config) {
    (void)config;
    size_t count=(size_t)s->channels*s->filter_length;
    s->algorithm.w=calloc(count,sizeof(float));
    s->algorithm.xbuf=calloc(count,sizeof(float));
    s->algorithm.xf=calloc(count,sizeof(float));
    s->algorithm.pos=calloc((size_t)s->channels,sizeof(uint32_t));
    return s->algorithm.w && s->algorithm.xbuf && s->algorithm.xf && s->algorithm.pos
        ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

int orpheus_builtin_anc_fxlms_destroy(orpheus_builtin_anc_fxlms_State* s) {
    free(s->algorithm.w);free(s->algorithm.xbuf);free(s->algorithm.xf);free(s->algorithm.pos);
    s->algorithm.w=NULL;s->algorithm.xbuf=NULL;s->algorithm.xf=NULL;s->algorithm.pos=NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_anc_fxlms_reset(orpheus_builtin_anc_fxlms_State* s) {
    if (!s->algorithm.w || !s->algorithm.xbuf || !s->algorithm.xf || !s->algorithm.pos)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)s->channels*s->filter_length*sizeof(float);
    memset(s->algorithm.w,0,bytes);memset(s->algorithm.xbuf,0,bytes);memset(s->algorithm.xf,0,bytes);
    memset(s->algorithm.pos,0,(size_t)s->channels*sizeof(uint32_t));
    s->algorithm.acc_d2=0;s->algorithm.acc_e2=0;
    s->algorithm.total_frames=0;s->algorithm.total_samples=0;
    s->power_d=0;s->power_e=0;s->noise_reduction_db=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_anc_fxlms_get_parameter(orpheus_builtin_anc_fxlms_State* s,
    const char* key, OrpheusValue* value) {
    if (strcmp(key,"detail")) return ORPHEUS_ERR_NOT_FOUND;
    if (!isfinite(s->noise_reduction_db) || !isfinite(s->power_d) || !isfinite(s->power_e))
        return ORPHEUS_ERR_INVALID_ARG;
    int length=snprintf(s->algorithm.json_detail,sizeof(s->algorithm.json_detail),
        "{\"noise_reduction_db\":%.2f,\"power_d\":%.6g,\"power_e\":%.6g,\"frames\":%llu}",
        (double)s->noise_reduction_db,(double)s->power_d,(double)s->power_e,
        (unsigned long long)s->algorithm.total_frames);
    if (length<0 || (size_t)length>=sizeof(s->algorithm.json_detail)) return ORPHEUS_ERR_PROCESSING;
    value->type=ORPHEUS_VALUE_STRING;value->value.str=s->algorithm.json_detail;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

static bool private_overlap(const orpheus_builtin_anc_fxlms_State* s, const void* data, size_t bytes) {
    size_t history_bytes=(size_t)s->channels*s->filter_length*sizeof(float);
    return overlap(data,bytes,s,sizeof(*s)) || overlap(data,bytes,s->algorithm.w,history_bytes) ||
        overlap(data,bytes,s->algorithm.xbuf,history_bytes) || overlap(data,bytes,s->algorithm.xf,history_bytes) ||
        overlap(data,bytes,s->algorithm.pos,(size_t)s->channels*sizeof(uint32_t));
}

int orpheus_builtin_anc_fxlms_algorithm(orpheus_builtin_anc_fxlms_State* s,
    orpheus_builtin_anc_fxlms_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!s->algorithm.w || !s->algorithm.xbuf || !s->algorithm.xf || !s->algorithm.pos ||
        !io->x.buffer || !io->d.buffer || !io->out.buffer ||
        io->x.buffer->frame_count<ctx->frame_count || io->d.buffer->frame_count<ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if (private_overlap(s,io->x.samples,bytes) || private_overlap(s,io->d.samples,bytes) ||
        private_overlap(s,io->out.samples,bytes) || overlap(io->x.samples,bytes,io->out.samples,bytes) ||
        overlap(io->d.samples,bytes,io->out.samples,bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t channels=(uint32_t)s->channels,length=(uint32_t)s->filter_length;
    uint32_t delay=(uint32_t)s->secondary_delay;
    uint64_t samples=(uint64_t)ctx->frame_count*channels;
    if (s->algorithm.total_frames==UINT64_MAX || s->algorithm.total_samples>UINT64_MAX-samples)
        return ORPHEUS_ERR_PROCESSING;
    double pd=0,pe=0;
    for (uint32_t c=0;c<channels;++c) {
        float* w=s->algorithm.w+(size_t)c*length;
        float* x=s->algorithm.xbuf+(size_t)c*length;
        float* xf=s->algorithm.xf+(size_t)c*length;
        uint32_t p=s->algorithm.pos[c];
        for (uint32_t n=0;n<ctx->frame_count;++n) {
            x[p]=io->x.samples[n*channels+c];
            float norm=0;
            for (uint32_t k=0;k<length;++k) {
                uint32_t index=(p+length-k)%length;
                uint32_t filtered=(p+length-((k+delay)%length))%length;
                float value=s->secondary_gain*x[filtered];
                xf[index]=value;
                norm+=value*value;
            }
            float y=0;
            for (uint32_t k=0;k<length;++k)
                y+=w[k]*x[(p+length-k)%length];
            float d=io->d.samples[n*channels+c];
            float error=d-s->secondary_gain*y;
            pd+=(double)(d*d);pe+=(double)(error*error);
            float scale=s->step_size*error/(norm+s->eps);
            for (uint32_t k=0;k<length;++k)
                w[k]=s->leakage*w[k]+scale*xf[(p+length-k)%length];
            io->out.samples[n*channels+c]=-y;
            p=(p+1)%length;
        }
        s->algorithm.pos[c]=p;
    }
    s->algorithm.acc_d2+=pd;s->algorithm.acc_e2+=pe;
    s->algorithm.total_frames++;s->algorithm.total_samples+=samples;
    s->power_d=(float)(s->algorithm.acc_d2/(double)s->algorithm.total_samples);
    s->power_e=(float)(s->algorithm.acc_e2/(double)s->algorithm.total_samples);
    s->noise_reduction_db=s->power_e>1e-30f
        ? 10.0f*log10f(s->power_d/(s->power_e+1e-30f)) : 0;
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_anc_fxlms_emit_out(ctx,&io->out);
}
