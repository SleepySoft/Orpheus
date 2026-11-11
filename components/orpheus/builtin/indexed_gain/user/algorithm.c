#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SILENT 5.0118723e-7f

static int parse(const char* text,float* values,uint32_t* count) {
    *count=0;
    while(isspace((unsigned char)*text)) ++text;
    if(!*text) return ORPHEUS_ERR_INVALID_ARG;
    for(unsigned n=0;;n++) {
        if(n==32) return ORPHEUS_ERR_INVALID_ARG;
        char* end;errno=0;
        float value=strtof(text,&end);
        if(end==text || errno==ERANGE || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        values[n]=value;*count=n+1;
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
}

static float target(float index,float offset,float maximum,const float* axis,const float* table,uint32_t count) {
    index=fminf(255.0f,fmaxf(0.0f,index));
    float db=table[0];
    if(index>axis[0]) for(uint32_t n=1;n<count;n++) if(index<=axis[n]) {
        float span=axis[n]-axis[n-1];
        float fraction=span!=0 ? (index-axis[n-1])/span : 1;
        db=table[n-1]+fraction*(table[n]-table[n-1]);
        break;
    }
    db+=fminf(0.0f,fmaxf(-10.0f,offset));
    return powf(10.0f,fminf(maximum,db)/20.0f);
}

int orpheus_builtin_indexed_gain_validate(const orpheus_builtin_indexed_gain_Parameters* p) {
    float axis[32],table[32];uint32_t nx,ny;
    int rc=parse(p->table_idx,axis,&nx);
    if(rc) return rc;
    rc=parse(p->table_db,table,&ny);
    if(rc) return rc;
    if(nx!=ny) return ORPHEUS_ERR_INVALID_ARG;
    for(uint32_t n=1;n<nx;n++)
        if(axis[n]<axis[n-1] || !isfinite(axis[n]-axis[n-1]) || !isfinite(table[n]-table[n-1]))
            return ORPHEUS_ERR_INVALID_ARG;
    return isfinite(target(p->gain_index,p->gain_offset_db,p->max_gain_db,axis,table,nx))
        ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_indexed_gain_prepare(orpheus_builtin_indexed_gain_State* s,const OrpheusConfig* config) {
    uint32_t count;
    int rc=parse(s->table_idx,s->algorithm.table_idx,&s->algorithm.table_size);
    if(rc) return rc;
    rc=parse(s->table_db,s->algorithm.table_db,&count);
    if(rc) return rc;
    s->algorithm.block_size=config->block_size ? config->block_size : 32;
    s->algorithm.sample_rate=config->sample_rate ? (float)config->sample_rate : 48000.0f;
    s->current_gain=s->apply_index_on_prepare ? target(s->gain_index,s->gain_offset_db,s->max_gain_db,
        s->algorithm.table_idx,s->algorithm.table_db,s->algorithm.table_size) : s->initial_gain;
    s->algorithm.target_gain=s->current_gain;
    s->algorithm.ramp_coeff=1;
    s->algorithm.remaining=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_indexed_gain_reset(orpheus_builtin_indexed_gain_State* s) {
    s->algorithm.remaining=0;
    s->algorithm.ramp_coeff=1;
    return ORPHEUS_OK;
}

int orpheus_builtin_indexed_gain_set_parameter(orpheus_builtin_indexed_gain_State* s,
    const char* key,const OrpheusValue* value) {
    bool index=!strcmp(key,"gain_index"),offset=!strcmp(key,"gain_offset_db");
    if(!index && !offset) return ORPHEUS_OK;
    float next=fmaxf(target(index ? value->value.f32 : s->gain_index,
        offset ? value->value.f32 : s->gain_offset_db,s->max_gain_db,
        s->algorithm.table_idx,s->algorithm.table_db,s->algorithm.table_size),SILENT);
    float current=fmaxf(s->current_gain,SILENT);
    float ratio=next/current,difference=fabsf(20*log10f(ratio));
    if(!isfinite(ratio) || !isfinite(difference)) return ORPHEUS_ERR_INVALID_ARG;
    if(difference<.01f || s->ramp_ms<=0) {
        s->current_gain=s->algorithm.target_gain=next;
        s->algorithm.remaining=0;s->algorithm.ramp_coeff=1;
        return ORPHEUS_OK;
    }
    float ms=s->ramp_ms;
    if(s->ramp_db_per_s>0) ms=difference/fminf(50000,fmaxf(5,s->ramp_db_per_s))*1000;
    ms=fminf(3000,fmaxf(1,ms));
    float blocks=ceilf(ms/(1000*(float)s->algorithm.block_size/s->algorithm.sample_rate));
    if(!isfinite(blocks) || blocks>=4294967296.0f) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t remaining=blocks<1 ? 1 : (uint32_t)blocks;
    float coefficient=expf(logf(ratio)/((float)s->algorithm.block_size*remaining));
    if(!isfinite(coefficient)) return ORPHEUS_ERR_INVALID_ARG;
    s->algorithm.target_gain=next;s->algorithm.remaining=remaining;s->algorithm.ramp_coeff=coefficient;
    return ORPHEUS_OK;
}

int orpheus_builtin_indexed_gain_algorithm(orpheus_builtin_indexed_gain_State* s,
    orpheus_builtin_indexed_gain_IO* io,const OrpheusProcessContext* ctx) {
    if(!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    uintptr_t x=(uintptr_t)io->in.samples,y=(uintptr_t)io->out.samples;
    uintptr_t state=(uintptr_t)s;
    if((x!=y && (x>=y ? x-y<bytes : y-x<bytes)) ||
        (bytes && (y>=state ? y-state<sizeof(*s) : state-y<bytes)) ||
        (bytes && (x>=state ? x-state<sizeof(*s) : state-x<bytes))) return ORPHEUS_ERR_INVALID_ARG;
    for(uint32_t n=0;n<ctx->frame_count;n++) {
        if(!s->freeze && s->algorithm.remaining) s->current_gain*=s->algorithm.ramp_coeff;
        for(int32_t c=0;c<s->channels;c++) {
            size_t i=(size_t)n*s->channels+c;
            io->out.samples[i]=io->in.samples[i]*s->current_gain;
        }
    }
    if(!s->freeze && s->algorithm.remaining && --s->algorithm.remaining==0) {
        s->current_gain=s->algorithm.target_gain;s->algorithm.ramp_coeff=1;
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_indexed_gain_emit_out(ctx,&io->out);
}
