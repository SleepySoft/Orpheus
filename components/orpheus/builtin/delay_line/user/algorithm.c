#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse_delays(const char* text, uint32_t channels, int32_t maximum, bool integer, void* output) {
    if (output) memset(output,0,64*sizeof(int32_t));
    if (!text[0]) return ORPHEUS_OK;
    for (uint32_t c=0;c<channels;++c) {
        char* end;
        errno=0;
        if (integer) {
            long value=strtol(text,&end,10);
            if (end==text || errno==ERANGE || value<0 || value>maximum) return ORPHEUS_ERR_INVALID_ARG;
            if (output) ((int32_t*)output)[c]=(int32_t)value;
        } else {
            float value=strtof(text,&end);
            if (end==text || errno==ERANGE || !isfinite(value) || value<0 || value>maximum)
                return ORPHEUS_ERR_INVALID_ARG;
            if (output) ((float*)output)[c]=value;
        }
        text=end;
        while (isspace((unsigned char)*text)) ++text;
        if (!*text) return integer && c+1!=channels ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
        if (c+1==channels || *text++!=',' || !*text) return ORPHEUS_ERR_INVALID_ARG;
    }
    return ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_delay_line_validate(const orpheus_builtin_delay_line_Parameters* p) {
    if (p->delay_samples>p->max_delay_samples) return ORPHEUS_ERR_INVALID_ARG;
    for (int32_t c=0;c<p->channels;++c)
        if (p->delays_i32[c]>p->max_delay_samples) return ORPHEUS_ERR_INVALID_ARG;
    int rc=parse_delays(p->delays_samples,(uint32_t)p->channels,p->max_delay_samples,false,NULL);
    return rc ? rc : parse_delays(p->initial_delays_i32,(uint32_t)p->channels,p->max_delay_samples,true,NULL);
}

int orpheus_builtin_delay_line_prepare(orpheus_builtin_delay_line_State* s, const OrpheusConfig* config) {
    int rc=parse_delays(s->delays_samples,(uint32_t)s->channels,s->max_delay_samples,false,s->algorithm.delays_samples);
    if (rc) return rc;
    if (s->initial_delays_i32[0]) {
        rc=parse_delays(s->initial_delays_i32,(uint32_t)s->channels,s->max_delay_samples,true,s->delays_i32);
        if (rc) return rc;
    }
    uint32_t frames=config->block_size ? config->block_size : 32;
    uint64_t count=((uint64_t)s->max_delay_samples+frames+1)*(uint32_t)s->channels;
    if (count>UINT32_MAX || count>SIZE_MAX/sizeof(float)) return ORPHEUS_ERR_OUT_OF_MEMORY;
    s->algorithm.buffer=calloc((size_t)count,sizeof(float));
    if (!s->algorithm.buffer) return ORPHEUS_ERR_OUT_OF_MEMORY;
    s->algorithm.capacity=(uint32_t)count;
    s->algorithm.write_pos=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_delay_line_destroy(orpheus_builtin_delay_line_State* s) {
    free(s->algorithm.buffer);
    s->algorithm.buffer=NULL;
    s->algorithm.capacity=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_delay_line_reset(orpheus_builtin_delay_line_State* s) {
    if (s->algorithm.buffer)
        memset(s->algorithm.buffer,0,(size_t)s->algorithm.capacity*sizeof(float));
    s->algorithm.write_pos=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_delay_line_set_parameter(orpheus_builtin_delay_line_State* s,
    const char* key, const OrpheusValue* value) {
    (void)s; (void)value;
    return !strcmp(key,"delay_samples") ? ORPHEUS_OK : ORPHEUS_ERR_UNSUPPORTED;
}

int orpheus_builtin_delay_line_register_slots(orpheus_builtin_delay_line_State* s, const OrpheusRegistry* reg) {
    if (ORPHEUS_REG_ARRAY(reg,s,algorithm.delays_samples,s->channels,ORPHEUS_SLOT_BULK,
        "delays_samples","每通道延迟样本数",ORPHEUS_VALUE_FLOAT,
        .min_f32=0,.max_f32=(float)s->max_delay_samples,.flags=ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_ARRAY(reg,s,delays_i32,s->channels,ORPHEUS_SLOT_BULK,
        "delays_i32","每通道整数延迟样本数",ORPHEUS_VALUE_INT,
        .min_i32=0,.max_i32=s->max_delay_samples,.update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
        .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED | ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}

static bool overlaps(const void* a, size_t a_bytes, const void* b, size_t b_bytes) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<b_bytes : y-x<a_bytes;
}

int orpheus_builtin_delay_line_algorithm(orpheus_builtin_delay_line_State* s,
    orpheus_builtin_delay_line_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!s->algorithm.buffer || !io->in.buffer || !io->out.buffer ||
        io->in.buffer->frame_count<ctx->frame_count) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t channels=(uint32_t)s->channels, delays[64];
    for (uint32_t c=0;c<channels;++c) {
        if (s->delay_samples>=0) delays[c]=(uint32_t)s->delay_samples;
        else if (s->delay_vector_mode) {
            if (s->delays_i32[c]<0 || s->delays_i32[c]>s->max_delay_samples) return ORPHEUS_ERR_INVALID_ARG;
            delays[c]=(uint32_t)s->delays_i32[c];
        } else {
            float value=s->algorithm.delays_samples[c];
            if (!isfinite(value) || value<0 || value>s->max_delay_samples) return ORPHEUS_ERR_INVALID_ARG;
            delays[c]=(uint32_t)(value+0.5f);
        }
    }
    size_t bytes=(size_t)ctx->frame_count*channels*sizeof(float);
    size_t ring_bytes=(size_t)s->algorithm.capacity*sizeof(float);
    if (overlaps(io->in.samples,bytes,io->out.samples,bytes) ||
        overlaps(io->in.samples,bytes,s->algorithm.buffer,ring_bytes) ||
        overlaps(io->out.samples,bytes,s->algorithm.buffer,ring_bytes)) return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t n=0;n<ctx->frame_count;++n) {
        for (uint32_t c=0;c<channels;++c) {
            s->algorithm.buffer[s->algorithm.write_pos+c]=io->in.samples[n*channels+c];
            uint32_t index=(uint32_t)(((uint64_t)s->algorithm.write_pos+s->algorithm.capacity-
                delays[c]*channels)%s->algorithm.capacity);
            io->out.samples[n*channels+c]=s->algorithm.buffer[index+c];
        }
        s->algorithm.write_pos=(s->algorithm.write_pos+channels)%s->algorithm.capacity;
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_delay_line_emit_out(ctx,&io->out);
}
