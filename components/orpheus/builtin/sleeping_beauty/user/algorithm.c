#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse_table(const char* text, float* output, unsigned* parsed) {
    unsigned count=0;
    while (isspace((unsigned char)*text)) ++text;
    if (!*text) return ORPHEUS_ERR_INVALID_ARG;
    for (;;) {
        char* end;
        float value=(float)strtod(text,&end);
        if (end==text || !isfinite(value) || count==30) return ORPHEUS_ERR_INVALID_ARG;
        output[count++]=value;
        while (isspace((unsigned char)*end)) ++end;
        if (!*end) break;
        if (*end!=',') return ORPHEUS_ERR_INVALID_ARG;
        text=end+1;
    }
    *parsed=count;
    return ORPHEUS_OK;
}

static int parse_map(const char* text, int32_t* output, unsigned channels) {
    for (unsigned c=0;c<channels;++c) output[c]=-1;
    while (isspace((unsigned char)*text)) ++text;
    if (!*text) return ORPHEUS_OK;
    for (unsigned c=0;c<64;++c) {
        char* end;errno=0;
        long value=strtol(text,&end,10);
        if (end==text || errno==ERANGE || value<-1 || value>3) return ORPHEUS_ERR_INVALID_ARG;
        if (c<channels) output[c]=(int32_t)value;
        while (isspace((unsigned char)*end)) ++end;
        if (!*end) return ORPHEUS_OK;
        if (*end!=',') return ORPHEUS_ERR_INVALID_ARG;
        text=end+1;
    }
    return ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_sleeping_beauty_validate(const orpheus_builtin_sleeping_beauty_Parameters* p) {
    if (strcmp(p->center_mono_mode,"attenuated") && strcmp(p->center_mono_mode,"mean_lr"))
        return ORPHEUS_ERR_INVALID_ARG;
    float axis[30],values[30];unsigned nx=0,ny=0;int32_t map[64];
    if (parse_table(p->table_idx,axis,&nx) || parse_table(p->table_db,values,&ny) || nx!=ny ||
        parse_map(p->chan_map,map,(unsigned)p->channels)) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned i=1;i<nx;++i)
        if (axis[i]<=axis[i-1] || !isfinite(axis[i]-axis[i-1])) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned i=0;i<ny;++i)
        if (!isfinite(powf(10.0f,values[i]/20.0f))) return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}

static void calculate_gains(const orpheus_builtin_sleeping_beauty_State* s, float target[4]) {
    float index=s->gain_index,cut=0;
    for (unsigned j=0;j<s->algorithm.table_size;++j) {
        float point=s->algorithm.table_idx[j];
        if (index>point) continue;
        if (!j) {
            cut=powf(10.0f,s->algorithm.table_db[0]/20.0f);
            if (point!=0) cut=(index/point)*cut;
        } else {
            float previous=s->algorithm.table_idx[j-1],db=s->algorithm.table_db[j-1];
            float denominator=point-previous;
            if (denominator!=0) db=((index-previous)/denominator)*(s->algorithm.table_db[j]-db)+db;
            else db=s->algorithm.table_db[j];
            cut=powf(10.0f,db/20.0f);
        }
        break;
    }
    float delta=index-s->offset;
    bool extreme=fabsf(delta)>=s->offset-1;
    float left=delta>0 ? cut : 1,right=delta>0 ? 1 : cut,center=cut,mono=cut;
    if (extreme) {
        if (delta>0) left=0;else right=0;
        center=0;
        if (s->mutes_bass!=0) mono=0;
    } else if (!strcmp(s->center_mono_mode,"mean_lr")) {
        center=(left+right)*.5f;mono=center;
    }
    target[0]=left;target[1]=right;target[2]=center;target[3]=mono;
}

static int set_target(SBRamper* r, float target, const orpheus_builtin_sleeping_beauty_State* s) {
    if (!isfinite(target)) return ORPHEUS_ERR_INVALID_ARG;
    r->target_gain=fmaxf(target,SB_SILENT_GAIN);
    float current_db=20.0f*log10f(fmaxf(r->current_gain,SB_SILENT_GAIN));
    float target_db=20.0f*log10f(r->target_gain),diff=fabsf(target_db-current_db);
    if (diff<.01f || s->ramp_ms<=0) {
        r->coefficient=1;r->remaining=0;r->current_gain=r->target_gain;
    } else {
        float ms=s->ramp_ms;
        if (s->ramp_db_per_s>0) {
            float rate=fminf(50000.0f,fmaxf(5.0f,s->ramp_db_per_s));
            ms=diff/rate*1000.0f;
        }
        ms=fminf(3000.0f,fmaxf(1.0f,ms));
        float block_ms=1000.0f*(float)s->algorithm.block_size/s->algorithm.sample_rate;
        float blocks=ceilf(ms/block_ms);
        if (!isfinite(blocks) || blocks>=4294967296.0f) return ORPHEUS_ERR_INVALID_ARG;
        r->remaining=blocks<1 ? 1 : (uint32_t)blocks;
        r->coefficient=expf(logf(r->target_gain/fmaxf(r->current_gain,SB_SILENT_GAIN))/
            ((float)s->algorithm.block_size*(float)r->remaining));
        if (!isfinite(r->coefficient)) return ORPHEUS_ERR_INVALID_ARG;
    }
    return ORPHEUS_OK;
}

static int recalculate(orpheus_builtin_sleeping_beauty_State* s) {
    float targets[4];calculate_gains(s,targets);
    SBRamper candidates[4];memcpy(candidates,s->algorithm.rampers,sizeof(candidates));
    for (unsigned r=0;r<4;++r) {
        int rc=set_target(candidates+r,targets[r],s);
        if (rc) return rc;
    }
    memcpy(s->algorithm.rampers,candidates,sizeof(candidates));
    s->algorithm.last_index=s->gain_index;s->algorithm.last_ms=s->ramp_ms;
    s->algorithm.last_rate=s->ramp_db_per_s;s->algorithm.controls_dirty=false;
    return ORPHEUS_OK;
}

static void unity(orpheus_builtin_sleeping_beauty_State* s) {
    for (unsigned r=0;r<4;++r) s->algorithm.rampers[r]=(SBRamper){1,1,1,0};
}

static void snap(orpheus_builtin_sleeping_beauty_State* s) {
    for (unsigned r=0;r<4;++r) {
        SBRamper* p=s->algorithm.rampers+r;
        p->current_gain=p->target_gain;p->coefficient=1;p->remaining=0;
    }
}

int orpheus_builtin_sleeping_beauty_prepare(orpheus_builtin_sleeping_beauty_State* s,
    const OrpheusConfig* config) {
    if (!isfinite(config->sample_rate) || config->sample_rate<0) return ORPHEUS_ERR_INVALID_ARG;
    float rate=config->sample_rate ? (float)config->sample_rate : 48000.0f;
    if (!isfinite(rate) || rate<=0) return ORPHEUS_ERR_INVALID_ARG;
    memset(&s->algorithm,0,sizeof(s->algorithm));
    s->algorithm.sample_rate=rate;s->algorithm.block_size=config->block_size ? config->block_size : 32;
    unsigned db_count=0;
    int rc=parse_table(s->table_idx,s->algorithm.table_idx,&s->algorithm.table_size);
    if (rc) return rc;
    rc=parse_table(s->table_db,s->algorithm.table_db,&db_count);
    if (rc) return rc;
    rc=parse_map(s->chan_map,s->algorithm.channel_map,(unsigned)s->channels);
    if (rc) return rc;
    unity(s);
    s->algorithm.last_index=s->gain_index;s->algorithm.last_ms=s->ramp_ms;
    s->algorithm.last_rate=s->ramp_db_per_s;
    if (s->apply_index_on_prepare) {
        rc=recalculate(s);
        if (rc) return rc;
        snap(s);
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_sleeping_beauty_reset(orpheus_builtin_sleeping_beauty_State* s) {
    if (s->preserve_state_on_reset) return ORPHEUS_OK;
    unity(s);
    if (s->apply_index_on_prepare) {
        s->gain_index=s->offset;
        int rc=recalculate(s);
        if (rc) return rc;
        snap(s);
    }
    s->algorithm.last_index=s->gain_index;s->algorithm.last_ms=s->ramp_ms;
    s->algorithm.last_rate=s->ramp_db_per_s;s->algorithm.controls_dirty=false;
    return ORPHEUS_OK;
}

int orpheus_builtin_sleeping_beauty_set_parameter(orpheus_builtin_sleeping_beauty_State* s,
    const char* key,const OrpheusValue* value) {
    (void)value;
    if (!strcmp(key,"gain_index") || !strcmp(key,"ramp_ms") || !strcmp(key,"ramp_db_per_s")) {
        s->algorithm.controls_dirty=true;return ORPHEUS_OK;
    }
    return !strcmp(key,"freeze") ? ORPHEUS_OK : ORPHEUS_ERR_UNSUPPORTED;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_sleeping_beauty_algorithm(orpheus_builtin_sleeping_beauty_State* s,
    orpheus_builtin_sleeping_beauty_IO* io,const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    if (!s->algorithm.table_size || !s->algorithm.block_size || s->algorithm.sample_rate<=0 ||
        !isfinite(s->gain_index) || !isfinite(s->ramp_ms) || !isfinite(s->ramp_db_per_s))
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if (overlap(io->in.samples,bytes,s,sizeof(*s)) || overlap(io->out.samples,bytes,s,sizeof(*s)) ||
        (io->in.samples!=io->out.samples && overlap(io->in.samples,bytes,io->out.samples,bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    if (s->algorithm.controls_dirty || s->algorithm.last_index!=s->gain_index ||
        s->algorithm.last_ms!=s->ramp_ms || s->algorithm.last_rate!=s->ramp_db_per_s) {
        int rc=recalculate(s);
        if (rc) return rc;
    }
    for (uint32_t n=0;n<ctx->frame_count;++n) {
        if (!s->freeze) for (unsigned r=0;r<4;++r) {
            SBRamper* p=s->algorithm.rampers+r;
            if (p->remaining) p->current_gain*=p->coefficient;
            if (p->current_gain<SB_SILENT_GAIN) p->current_gain=SB_SILENT_GAIN;
        }
        for (int32_t c=0;c<s->channels;++c) {
            int32_t group=s->algorithm.channel_map[c];
            float gain=group>=0 ? s->algorithm.rampers[group].current_gain : 0;
            io->out.samples[(size_t)n*s->channels+c]=io->in.samples[(size_t)n*s->channels+c]*gain;
        }
    }
    if (!s->freeze) for (unsigned r=0;r<4;++r) {
        SBRamper* p=s->algorithm.rampers+r;
        if (p->remaining && --p->remaining==0) {
            p->current_gain=p->target_gain;p->coefficient=1;
        }
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_sleeping_beauty_emit_out(ctx,&io->out);
}
