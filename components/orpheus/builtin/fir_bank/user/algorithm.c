#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse_integers(const char* text, uint32_t count, uint32_t limit, uint32_t* values) {
    for (uint32_t i=0;i<count;++i) {
        while (isspace((unsigned char)*text)) ++text;
        if (!isdigit((unsigned char)*text)) return ORPHEUS_ERR_INVALID_ARG;
        char* end;
        errno=0;
        unsigned long value=strtoul(text,&end,10);
        if (errno==ERANGE || value>limit) return ORPHEUS_ERR_INVALID_ARG;
        if (values) values[i]=(uint32_t)value;
        while (isspace((unsigned char)*end)) ++end;
        if (i+1==count) return *end ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
        if (*end!=',') return ORPHEUS_ERR_INVALID_ARG;
        text=end+1;
    }
    return ORPHEUS_ERR_INVALID_ARG;
}

static int mode_of(const char* mode) {
    if (!strcmp(mode,"shared")) return 0;
    if (!strcmp(mode,"per_channel")) return 1;
    if (!strcmp(mode,"banked")) return 2;
    return -1;
}

int orpheus_builtin_fir_bank_normalize(orpheus_builtin_fir_bank_Parameters* p, const char* key) {
    if (key) return ORPHEUS_OK;
    if (!p->filter_indices[0]) return ORPHEUS_OK;
    if (mode_of(p->coefs_mode)!=2) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t indices[64];
    int rc=parse_integers(p->filter_indices,(uint32_t)p->channels,(uint32_t)p->num_filters-1,indices);
    if (rc) return rc;
    for (int32_t c=0;c<p->channels;++c) p->filter_indices_i32[c]=(int32_t)indices[c];
    return ORPHEUS_OK;
}

static int parse_coefs(const char* text, uint32_t sets, uint32_t taps, float* output) {
    while (isspace((unsigned char)*text)) ++text;
    if (!*text) {
        if (output) {
            memset(output,0,(size_t)sets*taps*sizeof(float));
            for (uint32_t i=0;i<sets;++i) output[i*taps]=1.0f;
        }
        return ORPHEUS_OK;
    }
    for (uint32_t i=0;i<sets*taps;++i) {
        char* end;
        float value=strtof(text,&end);
        if (end==text || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        if (output) output[i]=value;
        while (isspace((unsigned char)*end)) ++end;
        if (i+1==sets*taps) return *end ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
        if (*end!=',') return ORPHEUS_ERR_INVALID_ARG;
        text=end+1;
    }
    return ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_fir_bank_validate(const orpheus_builtin_fir_bank_Parameters* p) {
    int mode=mode_of(p->coefs_mode);
    if (mode<0 || (mode!=1 && p->initial_coefs_per_channel[0]) ||
        (mode!=2 && p->initial_coefs_banked[0])) return ORPHEUS_ERR_INVALID_ARG;
    if (p->taps_per_channel[0]) {
        int rc=parse_integers(p->taps_per_channel,(uint32_t)p->channels,(uint32_t)p->num_taps,NULL);
        if (rc) return rc;
    }
    if (mode==2) for (int32_t c=0;c<p->channels;++c)
        if (p->filter_indices_i32[c]<0 || p->filter_indices_i32[c]>=p->num_filters)
            return ORPHEUS_ERR_INVALID_ARG;
    uint32_t sets=mode==2 ? (uint32_t)p->num_filters : mode==1 ? (uint32_t)p->channels : 1;
    if (sets*(uint32_t)p->num_taps>32768) return ORPHEUS_ERR_INVALID_ARG;
    const char* text=mode==1 && p->initial_coefs_per_channel[0] ? p->initial_coefs_per_channel :
        mode==2 && p->initial_coefs_banked[0] ? p->initial_coefs_banked : p->coefs;
    return parse_coefs(text,sets,(uint32_t)p->num_taps,NULL);
}

int orpheus_builtin_fir_bank_prepare(orpheus_builtin_fir_bank_State* s, const OrpheusConfig* config) {
    (void)config;
    int mode=mode_of(s->coefs_mode);
    uint32_t sets=mode==2 ? (uint32_t)s->num_filters : mode==1 ? (uint32_t)s->channels : 1;
    float* coefficients=mode==2 ? s->algorithm.coefficients.banked :
        mode==1 ? s->algorithm.coefficients.per_channel : s->algorithm.coefficients.shared;
    const char* text=mode==1 && s->initial_coefs_per_channel[0] ? s->initial_coefs_per_channel :
        mode==2 && s->initial_coefs_banked[0] ? s->initial_coefs_banked : s->coefs;
    int rc=parse_coefs(text,sets,(uint32_t)s->num_taps,coefficients);
    if (rc) return rc;
    for (int32_t c=0;c<s->channels;++c) s->algorithm.lengths[c]=(uint32_t)s->num_taps;
    if (s->taps_per_channel[0]) {
        rc=parse_integers(s->taps_per_channel,(uint32_t)s->channels,(uint32_t)s->num_taps,s->algorithm.lengths);
        if (rc) return rc;
    }
    s->algorithm.history=calloc((size_t)s->channels*s->num_taps,sizeof(float));
    return s->algorithm.history ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

int orpheus_builtin_fir_bank_destroy(orpheus_builtin_fir_bank_State* s) {
    free(s->algorithm.history);
    s->algorithm.history=NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_fir_bank_reset(orpheus_builtin_fir_bank_State* s) {
    if (!s->algorithm.history) return ORPHEUS_ERR_INVALID_ARG;
    memset(s->algorithm.history,0,(size_t)s->channels*s->num_taps*sizeof(float));
    memset(s->algorithm.positions,0,sizeof(s->algorithm.positions));
    return ORPHEUS_OK;
}

int orpheus_builtin_fir_bank_register_slots(orpheus_builtin_fir_bank_State* s, const OrpheusRegistry* reg) {
    if (mode_of(s->coefs_mode)!=2) return ORPHEUS_OK;
    return ORPHEUS_REG_ARRAY(reg,s,filter_indices_i32,s->channels,ORPHEUS_SLOT_BULK,
        "filter_indices_i32","活动系数集索引",ORPHEUS_VALUE_INT,.min_i32=0,.max_i32=s->num_filters-1,
        .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
        .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED | ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID
        ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_fir_bank_algorithm(orpheus_builtin_fir_bank_State* s,
    orpheus_builtin_fir_bank_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->out.buffer || !s->algorithm.history) return ORPHEUS_ERR_INVALID_ARG;
    int mode=mode_of(s->coefs_mode);
    if (mode==2) for (int32_t c=0;c<s->channels;++c)
        if (s->filter_indices_i32[c]<0 || s->filter_indices_i32[c]>=s->num_filters)
            return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    size_t history_bytes=(size_t)s->channels*s->num_taps*sizeof(float);
    if (overlap(io->out.samples,bytes,&s->algorithm,sizeof(s->algorithm)) ||
        overlap(io->out.samples,bytes,s->algorithm.history,history_bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!s->processing_enabled) memset(io->out.samples,0,bytes);
    else {
        if (!io->in.buffer || io->in.buffer->frame_count<ctx->frame_count ||
            (io->in.samples!=io->out.samples && overlap(io->in.samples,bytes,io->out.samples,bytes)) ||
            overlap(io->in.samples,bytes,s->algorithm.history,history_bytes) ||
            overlap(io->in.samples,bytes,&s->algorithm,sizeof(s->algorithm)))
            return ORPHEUS_ERR_INVALID_ARG;
        const float* coefficients=mode==2 ? s->algorithm.coefficients.banked :
            mode==1 ? s->algorithm.coefficients.per_channel : s->algorithm.coefficients.shared;
        for (uint32_t f=0;f<ctx->frame_count;++f) for (int32_t c=0;c<s->channels;++c) {
            uint32_t length=s->algorithm.lengths[c];
            if (!length) { io->out.samples[(size_t)f*s->channels+c]=0.0f; continue; }
            float* history=s->algorithm.history+(size_t)c*s->num_taps;
            uint32_t filter=mode==2 ? (uint32_t)s->filter_indices_i32[c] : mode==1 ? (uint32_t)c : 0;
            const float* coefs=coefficients+filter*s->num_taps;
            uint32_t position=s->algorithm.positions[c],index=position+1==length ? 0 : position+1;
            history[position]=io->in.samples[(size_t)f*s->channels+c];
            float sum=0.0f;
            for (uint32_t k=length;k>0;--k) {
                sum+=coefs[k-1]*history[index];
                index=index+1==length ? 0 : index+1;
            }
            io->out.samples[(size_t)f*s->channels+c]=sum;
            s->algorithm.positions[c]=position+1==length ? 0 : position+1;
        }
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_fir_bank_emit_out(ctx,&io->out);
}
