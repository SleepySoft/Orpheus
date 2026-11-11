#include "component_state.h"
#include "component_api.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_block_split_normalize(orpheus_builtin_block_split_Parameters* p,const char* key) {
    if(!key && (p->available_frames || p->underflows || p->overflows)) return ORPHEUS_ERR_INVALID_ARG;
    uint64_t count=(uint64_t)p->output_frames*(uint32_t)p->factor*(uint32_t)p->channels;
    return count>SIZE_MAX/sizeof(float) ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}
int orpheus_builtin_block_split_reset(orpheus_builtin_block_split_State* s) {
    s->available_frames=0;s->underflows=0;s->overflows=0;
    s->algorithm.cursor=0;s->algorithm.frame_index=0;s->algorithm.epoch=0;s->algorithm.timeline_flags=0;
    if(s->algorithm.block)
        memset(s->algorithm.block,0,(size_t)s->output_frames*s->factor*s->channels*sizeof(float));
    return ORPHEUS_OK;
}
int orpheus_builtin_block_split_prepare(orpheus_builtin_block_split_State* s,const OrpheusConfig* config) {
    (void)config;
    size_t count=(size_t)s->output_frames*s->factor*s->channels;
    s->algorithm.block=calloc(count,sizeof(float));
    if(!s->algorithm.block) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return orpheus_builtin_block_split_reset(s);
}
int orpheus_builtin_block_split_destroy(orpheus_builtin_block_split_State* s) {
    free(s->algorithm.block);s->algorithm.block=NULL;
    return ORPHEUS_OK;
}
int orpheus_builtin_block_split_set_parameter(orpheus_builtin_block_split_State* s,
    const char* key,const OrpheusValue* value) {
    (void)s;(void)key;(void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}
int orpheus_builtin_block_split_algorithm(orpheus_builtin_block_split_State* s,
    orpheus_builtin_block_split_IO* io,const OrpheusProcessContext* ctx) {
    if(!ctx->frame_count) return ORPHEUS_OK;
    if(!s->algorithm.block) return ORPHEUS_ERR_PROCESSING;
    uint32_t frames=(uint32_t)s->output_frames,input_frames=frames*(uint32_t)s->factor;
    size_t bytes=(size_t)input_frames*s->channels*sizeof(float);
    if(ctx->trigger_input==io->in.input_index) {
        if(!io->in.buffer || io->in.buffer->frame_count!=input_frames ||
           overlap(io->in.samples,bytes,s,sizeof(*s)) ||
           overlap(io->in.samples,bytes,s->algorithm.block,bytes) ||
           ctx->frame_index>UINT64_MAX-input_frames) return ORPHEUS_ERR_INVALID_ARG;
        if(s->available_frames) {
            if(s->overflows<INT_MAX) ++s->overflows;
            return ORPHEUS_ERR_PROCESSING;
        }
        memcpy(s->algorithm.block,io->in.samples,bytes);
        s->algorithm.cursor=0;s->algorithm.frame_index=ctx->frame_index;
        s->algorithm.epoch=ctx->epoch;s->algorithm.timeline_flags=ctx->timeline_flags;
        s->available_frames=(int32_t)input_frames;
        return ORPHEUS_OK;
    }
    if(ctx->trigger_input!=io->tick.input_index || ctx->frame_count!=frames || !io->out.buffer)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t output_bytes=(size_t)frames*s->channels*sizeof(float);
    if(overlap(io->out.samples,output_bytes,s,sizeof(*s)) ||
       overlap(io->out.samples,output_bytes,s->algorithm.block,bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    if(s->available_frames<(int32_t)frames) {
        if(s->underflows<INT_MAX) ++s->underflows;
        return ORPHEUS_ERR_PROCESSING;
    }
    memcpy(io->out.samples,s->algorithm.block+(size_t)s->algorithm.cursor*s->channels,output_bytes);
    io->out.delivery.frame_index=s->algorithm.frame_index+s->algorithm.cursor;
    io->out.delivery.epoch=s->algorithm.epoch;
    io->out.delivery.timeline_flags=s->algorithm.timeline_flags;
    if(s->algorithm.cursor) io->out.delivery.timeline_flags&=~ORPHEUS_TIMELINE_DISCONTINUITY;
    if(s->available_frames!=(int32_t)frames) io->out.delivery.timeline_flags&=~ORPHEUS_TIMELINE_EOS;
    s->algorithm.cursor+=frames;s->available_frames-=(int32_t)frames;
    return orpheus_builtin_block_split_emit_out(ctx,&io->out);
}
