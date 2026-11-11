#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SILENT_GAIN 5.0118723e-7f
#define UNITY_EPSILON 4.7683716e-7f

static int parse(const char* cursor,float* values) {
    for (unsigned i=0;i<5;i++) {
        char* end;
        float value=strtof(cursor,&end);
        if (end==cursor || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        while (isspace((unsigned char)*end)) ++end;
        if (i<4 ? *end!=',' : *end!='\0') return ORPHEUS_ERR_INVALID_ARG;
        values[i]=value;cursor=i<4 ? end+1 : end;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_indexed_tone_validate(const orpheus_builtin_indexed_tone_Parameters* p) {
    float coefs[5];
    if (p->ramp0_current_gain<0 || p->ramp0_target_gain<0 || p->ramp0_coefficient<0 ||
        p->ramp1_current_gain<0 || p->ramp1_target_gain<0 || p->ramp1_coefficient<0)
        return ORPHEUS_ERR_INVALID_ARG;
    int rc=parse(p->boost_coefs,coefs);
    return rc ? rc : parse(p->cut_coefs,coefs);
}

static float index_to_gain(const orpheus_builtin_indexed_tone_State* s,float index) {
    float scale=fabsf(s->scale)>1.0e-12f ? s->scale : 1.0f;
    float normalized=(index-s->offset)/scale;
    if (normalized>1) normalized=1;
    if (normalized<-1) normalized=-1;
    float db=normalized>=0 ? normalized*s->max_boost_db : normalized*s->max_cut_db;
    return powf(10.0f,db/20.0f);
}

static void configure_ramp(orpheus_builtin_indexed_tone_State* s,float target) {
    orpheus_builtin_indexed_tone_AlgorithmState* a=&s->algorithm;
    a->target_gain=fmaxf(target,SILENT_GAIN);
    float current=fmaxf(a->current_gain,SILENT_GAIN);
    float diff_db=fabsf(20.0f*log10f(a->target_gain/current));
    if (diff_db<.01f || s->ramp_ms<=0 || a->sample_rate<=0) {
        a->current_gain=a->target_gain;a->ramp_coeff=1;a->frame_count=0;return;
    }
    float duration=s->ramp_ms;
    if (s->ramp_db_per_s>0) {
        float rate=fminf(50000.0f,fmaxf(5.0f,s->ramp_db_per_s));
        duration=diff_db/rate*1000.0f;
    }
    duration=fminf(3000.0f,fmaxf(1.0f,duration));
    float block_ms=1000.0f*(float)a->block_size/a->sample_rate;
    a->frame_count=(uint32_t)ceilf(duration/block_ms);
    if (a->frame_count<1) a->frame_count=1;
    a->ramp_coeff=expf(logf(a->target_gain/current)/((float)a->block_size*(float)a->frame_count));
}

int orpheus_builtin_indexed_tone_reset(orpheus_builtin_indexed_tone_State* s) {
    memset(s->algorithm.z1,0,sizeof(s->algorithm.z1));
    memset(s->algorithm.z2,0,sizeof(s->algorithm.z2));
    if (!s->execution_mode) {
        s->algorithm.frame_count=0;s->algorithm.ramp_coeff=1;s->algorithm.crossing_phase=0;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_indexed_tone_prepare(orpheus_builtin_indexed_tone_State* s,const OrpheusConfig* config) {
    if (!isfinite(config->sample_rate) || config->sample_rate<0 || config->sample_rate>FLT_MAX)
        return ORPHEUS_ERR_INVALID_ARG;
    s->algorithm.sample_rate=config->sample_rate ? (float)config->sample_rate : 48000;
    s->algorithm.block_size=config->block_size ? config->block_size : 32;
    int rc=parse(s->boost_coefs,s->algorithm.boost_coefs);
    if (rc) return rc;
    rc=parse(s->cut_coefs,s->algorithm.cut_coefs);
    if (rc) return rc;
    s->algorithm.current_gain=index_to_gain(s,s->gain_index);
    s->algorithm.target_gain=s->algorithm.current_gain;
    s->algorithm.final_target_gain=s->algorithm.current_gain;
    s->algorithm.ramp_coeff=1;s->algorithm.frame_count=0;s->algorithm.crossing_phase=0;
    return orpheus_builtin_indexed_tone_reset(s);
}

int orpheus_builtin_indexed_tone_set_parameter(orpheus_builtin_indexed_tone_State* s,
    const char* key,const OrpheusValue* value) {
    bool index=!strcmp(key,"gain_index");
    if (index || !strcmp(key,"ramp_ms") || !strcmp(key,"ramp_db_per_s")) {
        if (s->execution_mode) return ORPHEUS_ERR_UNSUPPORTED;
        if (index) {
            orpheus_builtin_indexed_tone_AlgorithmState* a=&s->algorithm;
            a->final_target_gain=index_to_gain(s,value->value.f32);
            a->crossing_phase=(a->current_gain<1 && a->final_target_gain>1) ||
                (a->current_gain>1 && a->final_target_gain<1);
            float target=a->final_target_gain;
            if (a->crossing_phase) target=a->current_gain<1 ? 1-UNITY_EPSILON : 1+UNITY_EPSILON;
            configure_ramp(s,target);
        }
        return ORPHEUS_OK;
    }
    if (!strcmp(key,"freeze") || !strcmp(key,"phase") || !strncmp(key,"ramp0_",6) ||
        !strncmp(key,"ramp1_",6)) return ORPHEUS_OK;
    return ORPHEUS_ERR_NOT_FOUND;
}

static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}

int orpheus_builtin_indexed_tone_algorithm(orpheus_builtin_indexed_tone_State* s,
    orpheus_builtin_indexed_tone_IO* io,const OrpheusProcessContext* ctx) {
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count ||
        (io->in.samples!=io->out.samples && overlap(io->in.samples,bytes,io->out.samples,bytes)) ||
        overlap(io->in.samples,bytes,s,sizeof(*s)) || overlap(io->out.samples,bytes,s,sizeof(*s)))
        return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_indexed_tone_AlgorithmState* a=&s->algorithm;
    if (s->execution_mode) {
        if (!ctx->frame_count) return orpheus_builtin_indexed_tone_emit_out(ctx,&io->out);
        if (!s->freeze && s->phase==1 && !s->ramp0_remaining_blocks && s->ramp1_remaining_blocks>0)
            s->phase=2;
        float* current=s->phase==1 ? &s->ramp0_current_gain : &s->ramp1_current_gain;
        float target=s->phase==1 ? s->ramp0_target_gain : s->ramp1_target_gain;
        int32_t* remaining=s->phase==1 ? &s->ramp0_remaining_blocks : &s->ramp1_remaining_blocks;
        float gain=*current,coefficient=1;
        if (!s->freeze) {
            if (!*remaining) gain=target;
            else {
                coefficient=s->phase==1 ? s->ramp0_coefficient : s->ramp1_coefficient;
                if (*remaining>INT32_MIN) --*remaining;
            }
        }
        const float* coefs=gain<1 ? a->cut_coefs : a->boost_coefs;
        for (uint32_t n=0;n<ctx->frame_count;n++) {
            gain*=coefficient;
            for (int32_t c=0;c<s->channels;c++) {
                size_t i=(size_t)n*s->channels+c;
                float x=io->in.samples[i],filtered=x*coefs[0]+a->z1[c];
                if (s->filter_order==1) a->z1[c]=x*coefs[1]+coefs[3]*filtered;
                else {
                    a->z1[c]=coefs[1]==0 ? a->z2[c]-coefs[3]*filtered :
                        (x*coefs[1]+a->z2[c])-coefs[3]*filtered;
                    a->z2[c]=x*coefs[2]-coefs[4]*filtered;
                }
                io->out.samples[i]=(gain-1)*filtered+x;
            }
        }
        *current=gain;
    } else {
        for (uint32_t n=0;n<ctx->frame_count;n++) {
            if (!s->freeze && a->frame_count>0) a->current_gain*=a->ramp_coeff;
            const float* coefs=a->current_gain>=1 ? a->boost_coefs : a->cut_coefs;
            for (int32_t c=0;c<s->channels;c++) {
                size_t i=(size_t)n*s->channels+c;
                float x=io->in.samples[i],filtered=coefs[0]*x+a->z1[c];
                a->z1[c]=coefs[1]*x+a->z2[c]-coefs[3]*filtered;
                a->z2[c]=(s->filter_order>1 ? coefs[2]*x : 0.0f)-coefs[4]*filtered;
                io->out.samples[i]=x+(a->current_gain-1)*filtered;
            }
        }
        if (!s->freeze && a->frame_count>0 && --a->frame_count==0) {
            a->current_gain=a->target_gain;a->ramp_coeff=1;
            if (a->crossing_phase) { a->crossing_phase=0;configure_ramp(s,a->final_target_gain); }
        }
    }
    return orpheus_builtin_indexed_tone_emit_out(ctx,&io->out);
}
