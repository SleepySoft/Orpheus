#include "component_state.h"
#include "component_api.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_noise_detector_nlms_prepare(orpheus_builtin_noise_detector_nlms_State* s,
    const OrpheusConfig* config) {
    (void)config;
    size_t count=(size_t)s->channels*s->filter_length;
    s->algorithm.w=calloc(count,sizeof(float));s->algorithm.x=calloc(count,sizeof(float));
    s->algorithm.pos=calloc((size_t)s->channels,sizeof(uint32_t));
    return s->algorithm.w && s->algorithm.x && s->algorithm.pos ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

int orpheus_builtin_noise_detector_nlms_destroy(orpheus_builtin_noise_detector_nlms_State* s) {
    free(s->algorithm.w);free(s->algorithm.x);free(s->algorithm.pos);
    s->algorithm.w=NULL;s->algorithm.x=NULL;s->algorithm.pos=NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_noise_detector_nlms_reset(orpheus_builtin_noise_detector_nlms_State* s) {
    if (!s->algorithm.w || !s->algorithm.x || !s->algorithm.pos) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)s->channels*s->filter_length*sizeof(float);
    memset(s->algorithm.w,0,bytes);memset(s->algorithm.x,0,bytes);
    memset(s->algorithm.pos,0,(size_t)s->channels*sizeof(uint32_t));
    s->algorithm.total_calls=0;s->algorithm.noise_calls=0;s->algorithm.total_samples=0;
    s->algorithm.noisy_samples=0;s->algorithm.clicks_total=0;
    s->algorithm.accumulated_input=0;s->algorithm.accumulated_residue=0;
    s->residue_db=0;s->erle_db=0;s->noise_frames=0;s->noise_ratio=0;s->clicks=0;s->residue_pk=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_noise_detector_nlms_get_parameter(orpheus_builtin_noise_detector_nlms_State* s,
    const char* key, OrpheusValue* value) {
    if (strcmp(key,"detail")) return ORPHEUS_ERR_NOT_FOUND;
    if (!isfinite(s->residue_db) || !isfinite(s->erle_db) || !isfinite(s->residue_pk))
        return ORPHEUS_ERR_INVALID_ARG;
    int length=snprintf(s->algorithm.json_detail,sizeof(s->algorithm.json_detail),
        "{\"residue_db\":%.2f,\"erle_db\":%.2f,\"noise_frames\":%llu,\"total_frames\":%llu,"
        "\"noisy_samples\":%llu,\"total_samples\":%llu,\"clicks\":%llu,\"residue_pk\":%.4f}",
        (double)s->residue_db,(double)s->erle_db,(unsigned long long)s->algorithm.noise_calls,
        (unsigned long long)s->algorithm.total_calls,(unsigned long long)s->algorithm.noisy_samples,
        (unsigned long long)s->algorithm.total_samples,(unsigned long long)s->algorithm.clicks_total,
        (double)s->residue_pk);
    if (length<0 || (size_t)length>=sizeof(s->algorithm.json_detail)) return ORPHEUS_ERR_PROCESSING;
    value->type=ORPHEUS_VALUE_STRING;value->value.str=s->algorithm.json_detail;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

static bool private_overlap(const orpheus_builtin_noise_detector_nlms_State* s, const void* data, size_t bytes) {
    size_t history=(size_t)s->channels*s->filter_length*sizeof(float);
    return overlap(data,bytes,s,sizeof(*s)) || overlap(data,bytes,s->algorithm.w,history) ||
        overlap(data,bytes,s->algorithm.x,history) ||
        overlap(data,bytes,s->algorithm.pos,(size_t)s->channels*sizeof(uint32_t));
}

int orpheus_builtin_noise_detector_nlms_algorithm(orpheus_builtin_noise_detector_nlms_State* s,
    orpheus_builtin_noise_detector_nlms_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (ctx->trigger_input!=io->in.input_index || !s->algorithm.w || !s->algorithm.x || !s->algorithm.pos ||
        !io->ref.buffer || !io->in.buffer || io->ref.buffer->frame_count<ctx->frame_count ||
        io->in.buffer->frame_count<ctx->frame_count) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t channels=(uint32_t)s->channels,length=(uint32_t)s->filter_length;
    size_t bytes=(size_t)ctx->frame_count*channels*sizeof(float);
    if (private_overlap(s,io->ref.samples,bytes) || private_overlap(s,io->in.samples,bytes) ||
        (io->out.buffer && (private_overlap(s,io->out.samples,bytes) ||
         overlap(io->ref.samples,bytes,io->out.samples,bytes) || overlap(io->in.samples,bytes,io->out.samples,bytes))))
        return ORPHEUS_ERR_INVALID_ARG;
    uint64_t samples=(uint64_t)ctx->frame_count*channels;
    if (s->algorithm.total_calls==UINT64_MAX || s->algorithm.total_samples>UINT64_MAX-samples ||
        s->algorithm.noise_calls>s->algorithm.total_calls || s->algorithm.noisy_samples>s->algorithm.total_samples ||
        s->algorithm.clicks_total>s->algorithm.total_samples) return ORPHEUS_ERR_PROCESSING;
    double residue_energy=0,input_energy=0;
    uint64_t noisy=0,clicks=0;
    float peak=s->residue_pk;
    for (uint32_t c=0;c<channels;++c) {
        float* w=s->algorithm.w+(size_t)c*length;
        float* x=s->algorithm.x+(size_t)c*length;
        uint32_t p=s->algorithm.pos[c];
        for (uint32_t n=0;n<ctx->frame_count;++n) {
            x[p]=io->ref.samples[n*channels+c];
            float y=0,norm=0;
            for (uint32_t k=0;k<length;++k) {
                float raw=x[(p+length-k)%length];
                y+=w[k]*raw;norm+=raw*raw;
            }
            float input=io->in.samples[n*channels+c],error=input-y,absolute=fabsf(error);
            residue_energy+=(double)error*error;input_energy+=(double)input*input;
            if (absolute>s->time_thres) noisy++;
            if (absolute>0.2f) clicks++;
            if (absolute>peak) peak=absolute;
            float scale=s->step_size*error/(norm+s->eps);
            for (uint32_t k=0;k<length;++k)
                w[k]=s->leakage*w[k]+scale*x[(p+length-k)%length];
            p=(p+1)%length;
        }
        s->algorithm.pos[c]=p;
    }
    double db=10.0*log10((residue_energy+1e-12)/(input_energy+1e-12));
    if (db>s->frame_thres_db) s->algorithm.noise_calls++;
    s->algorithm.total_calls++;s->algorithm.total_samples+=samples;
    s->algorithm.noisy_samples+=noisy;s->algorithm.clicks_total+=clicks;
    s->algorithm.accumulated_input+=input_energy;s->algorithm.accumulated_residue+=residue_energy;
    s->residue_db=(float)db;
    s->erle_db=10.0f*log10f((float)((s->algorithm.accumulated_input+1e-12)/
        (s->algorithm.accumulated_residue+1e-12)));
    s->noise_frames=s->algorithm.noise_calls>INT32_MAX ? INT32_MAX : (int32_t)s->algorithm.noise_calls;
    s->clicks=s->algorithm.clicks_total>INT32_MAX ? INT32_MAX : (int32_t)s->algorithm.clicks_total;
    s->noise_ratio=(float)((double)s->algorithm.noise_calls/(double)s->algorithm.total_calls);
    s->residue_pk=peak;
    if (!io->out.buffer) return ORPHEUS_OK;
    memcpy(io->out.samples,io->in.samples,bytes);io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_noise_detector_nlms_emit_out(ctx,&io->out);
}
