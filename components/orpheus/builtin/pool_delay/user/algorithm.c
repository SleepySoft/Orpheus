#include "component_state.h"
#include "component_api.h"
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_pool_delay_validate(const orpheus_builtin_pool_delay_Parameters* p) {
    for (int32_t c=0;c<p->channels;++c)
        if (p->delays_i32[c]>p->pool_size_samples) return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}

int orpheus_builtin_pool_delay_prepare(orpheus_builtin_pool_delay_State* s, const OrpheusConfig* config) {
    (void)config;
    s->algorithm.buffer=calloc((size_t)s->pool_size_samples,sizeof(float));
    if (!s->algorithm.buffer) return ORPHEUS_ERR_OUT_OF_MEMORY;
    s->algorithm.cursor=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_pool_delay_destroy(orpheus_builtin_pool_delay_State* s) {
    free(s->algorithm.buffer);
    s->algorithm.buffer=NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_pool_delay_reset(orpheus_builtin_pool_delay_State* s) {
    if (!s->algorithm.buffer) return ORPHEUS_ERR_INVALID_ARG;
    memset(s->algorithm.buffer,0,(size_t)s->pool_size_samples*sizeof(float));
    s->algorithm.cursor=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_pool_delay_register_slots(orpheus_builtin_pool_delay_State* s, const OrpheusRegistry* reg) {
    return ORPHEUS_REG_ARRAY(reg,s,delays_i32,s->channels,ORPHEUS_SLOT_BULK,
        "delays_i32","各通道整数延迟",ORPHEUS_VALUE_INT,.min_i32=0,.max_i32=s->pool_size_samples,
        .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
        .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED | ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID
        ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
}

static uint32_t advance(uint32_t position, uint32_t offset, uint32_t capacity) {
    return (uint32_t)(((uint64_t)position+offset)%capacity);
}
static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_pool_delay_algorithm(orpheus_builtin_pool_delay_State* s,
    orpheus_builtin_pool_delay_IO* io, const OrpheusProcessContext* ctx) {
    if (!s->algorithm.buffer || !io->in.buffer || !io->out.buffer ||
        io->in.buffer->frame_count<ctx->frame_count) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    size_t pool_bytes=(size_t)s->pool_size_samples*sizeof(float);
    if (io->in.samples==io->out.samples || overlap(io->in.samples,bytes,io->out.samples,bytes) ||
        overlap(io->in.samples,bytes,s->algorithm.buffer,pool_bytes) ||
        overlap(io->out.samples,bytes,s->algorithm.buffer,pool_bytes)) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t capacity=(uint32_t)s->pool_size_samples,channels=(uint32_t)s->channels,frames=ctx->frame_count;
    if (!s->enabled) {
        if (bytes) memcpy(io->out.samples,io->in.samples,bytes);
    } else {
        for (uint32_t c=0;c<channels;++c)
            if (s->delays_i32[c]<0 || (uint32_t)s->delays_i32[c]>capacity) return ORPHEUS_ERR_INVALID_ARG;
        uint32_t base=s->algorithm.cursor;
        for (uint32_t c=0;c<channels;++c) {
            uint32_t delay=(uint32_t)s->delays_i32[c],partial=delay<frames ? delay : frames;
            uint32_t index=advance(base,1,capacity);
            for (uint32_t n=0;n<partial;++n) {
                memcpy(io->out.samples+(size_t)n*channels+c,s->algorithm.buffer+index,sizeof(float));
                index=advance(index,1,capacity);
            }
            for (uint32_t n=partial;n<frames;++n)
                memcpy(io->out.samples+(size_t)n*channels+c,io->in.samples+(size_t)(n-partial)*channels+c,sizeof(float));
            base=advance(base,delay,capacity);
        }
        s->algorithm.cursor=advance(s->algorithm.cursor,frames,capacity);
        base=s->algorithm.cursor;
        for (uint32_t c=0;c<channels;++c) {
            uint32_t delay=(uint32_t)s->delays_i32[c],partial=delay<frames ? delay : frames;
            uint32_t index=advance(base,delay-partial+1,capacity);
            for (uint32_t n=frames-partial;n<frames;++n) {
                memcpy(s->algorithm.buffer+index,io->in.samples+(size_t)n*channels+c,sizeof(float));
                index=advance(index,1,capacity);
            }
            base=advance(base,delay,capacity);
        }
    }
    io->out.buffer->frame_count=frames;
    return orpheus_builtin_pool_delay_emit_out(ctx,&io->out);
}
