#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse_stages(const char* text, uint32_t channels, uint32_t stages, uint32_t* output) {
    if (!*text) {
        if (output) for (uint32_t c=0;c<channels;++c) output[c]=stages;
        return ORPHEUS_OK;
    }
    for (uint32_t c=0;c<channels;++c) {
        while (isspace((unsigned char)*text)) ++text;
        if (!isdigit((unsigned char)*text)) return ORPHEUS_ERR_INVALID_ARG;
        char* end;
        errno=0;
        unsigned long value=strtoul(text,&end,10);
        if (errno==ERANGE || value>stages) return ORPHEUS_ERR_INVALID_ARG;
        if (output) output[c]=(uint32_t)value;
        while (isspace((unsigned char)*end)) ++end;
        if (c+1==channels) return *end ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
        if (*end!=',') return ORPHEUS_ERR_INVALID_ARG;
        text=end+1;
    }
    return ORPHEUS_ERR_INVALID_ARG;
}

static int parse_coefs(const char* text, uint32_t sets, uint32_t stages, float* output) {
    while (isspace((unsigned char)*text)) ++text;
    if (!*text) return ORPHEUS_OK;
    uint32_t width=5*stages;
    for (uint32_t i=0;i<sets*width;++i) {
        char* end;
        errno=0;
        float value=strtof(text,&end);
        if (end==text || (errno==ERANGE && (value==0.0f || !isfinite(value))))
            return ORPHEUS_ERR_INVALID_ARG;
        if (output) output[(i/width)*80+i%width]=value;
        while (isspace((unsigned char)*end)) ++end;
        if (!*end) return ORPHEUS_OK;
        if (*end!=',') return ORPHEUS_ERR_INVALID_ARG;
        text=end+1;
    }
    return ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_iir_bank_validate(const orpheus_builtin_iir_bank_Parameters* p) {
    if (strcmp(p->coefs_mode,"shared") && strcmp(p->coefs_mode,"per_channel"))
        return ORPHEUS_ERR_INVALID_ARG;
    bool per_channel=!strcmp(p->coefs_mode,"per_channel");
    if (!per_channel && p->initial_coefs_per_channel[0]) return ORPHEUS_ERR_INVALID_ARG;
    int rc=parse_stages(p->stages_per_channel,(uint32_t)p->channels,(uint32_t)p->num_stages,NULL);
    if (rc) return rc;
    return parse_coefs(per_channel && p->initial_coefs_per_channel[0]
        ? p->initial_coefs_per_channel : p->coefs,
        per_channel ? (uint32_t)p->channels : 1,(uint32_t)p->num_stages,NULL);
}

int orpheus_builtin_iir_bank_reset(orpheus_builtin_iir_bank_State* s) {
    memset(s->algorithm.channelStates,0,sizeof(s->algorithm.channelStates));
    return ORPHEUS_OK;
}

int orpheus_builtin_iir_bank_prepare(orpheus_builtin_iir_bank_State* s, const OrpheusConfig* config) {
    (void)config;
    memset(s->algorithm.shared,0,sizeof(s->algorithm.shared));
    memset(s->algorithm.per_channel,0,sizeof(s->algorithm.per_channel));
    for (uint32_t stage=0;stage<16;++stage) s->algorithm.shared[5*stage]=1.0f;
    for (uint32_t c=0;c<64;++c) for (uint32_t stage=0;stage<16;++stage)
        s->algorithm.per_channel[c*80+5*stage]=1.0f;
    int rc=parse_stages(s->stages_per_channel,(uint32_t)s->channels,(uint32_t)s->num_stages,
        s->algorithm.channelStages);
    if (rc) return rc;
    bool per_channel=!strcmp(s->coefs_mode,"per_channel");
    rc=parse_coefs(per_channel && s->initial_coefs_per_channel[0]
        ? s->initial_coefs_per_channel : s->coefs,
        per_channel ? (uint32_t)s->channels : 1,(uint32_t)s->num_stages,
        per_channel ? s->algorithm.per_channel : s->algorithm.shared);
    return rc ? rc : orpheus_builtin_iir_bank_reset(s);
}

int orpheus_builtin_iir_bank_register_slots(orpheus_builtin_iir_bank_State* s, const OrpheusRegistry* reg) {
    return !strcmp(s->coefs_mode,"per_channel")
        ? orpheus_builtin_iir_bank_register_initial_coefs_per_channel(s,reg)
        : orpheus_builtin_iir_bank_register_coefs(s,reg);
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_iir_bank_algorithm(orpheus_builtin_iir_bank_State* s,
    orpheus_builtin_iir_bank_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if (overlap(io->out.samples,bytes,&s->algorithm,sizeof(s->algorithm)))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!s->processing_enabled) memset(io->out.samples,0,bytes);
    else {
        if (!io->in.buffer || io->in.buffer->frame_count<ctx->frame_count ||
            overlap(io->in.samples,bytes,io->out.samples,bytes) ||
            overlap(io->in.samples,bytes,&s->algorithm,sizeof(s->algorithm)))
            return ORPHEUS_ERR_INVALID_ARG;
        bool per_channel=!strcmp(s->coefs_mode,"per_channel");
        for (uint32_t n=0;n<ctx->frame_count;++n) {
            for (int32_t c=0;c<s->channels;++c) {
                const float* coefficients=per_channel ? s->algorithm.per_channel+c*80 : s->algorithm.shared;
                orpheus_builtin_iir_bank_ChannelState* st=&s->algorithm.channelStates[c];
                float x=io->in.samples[(size_t)n*s->channels+c];
                for (uint32_t i=0;i<s->algorithm.channelStages[c];++i) {
                    float b0=coefficients[5*i],b1=coefficients[5*i+1],b2=coefficients[5*i+2];
                    float a1=coefficients[5*i+3],a2=coefficients[5*i+4];
                    float z1=st->z1[i],z2=st->z2[i];
                    float y=b0*x+z1;
                    st->z1[i]=b1*x+z2-a1*y;
                    st->z2[i]=b2*x-a2*y;
                    x=y;
                }
                io->out.samples[(size_t)n*s->channels+c]=x;
            }
        }
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_iir_bank_emit_out(ctx,&io->out);
}
