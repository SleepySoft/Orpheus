#include "component_state.h"
#include "component_api.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_downrate_normalize(orpheus_builtin_downrate_Parameters* p,const char* key) {
    if(!key && (p->available_frames || p->underflows || p->overflows)) return ORPHEUS_ERR_INVALID_ARG;
    uint64_t count=(uint64_t)p->input_frames*(uint32_t)p->factor*(uint32_t)p->channels;
    return count>SIZE_MAX/sizeof(float) ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}
int orpheus_builtin_downrate_reset(orpheus_builtin_downrate_State* s) {
    s->available_frames=0;s->underflows=0;s->overflows=0;
    s->algorithm.frame_index=0;s->algorithm.epoch=0;s->algorithm.timeline_flags=0;
    if(s->algorithm.block)
        memset(s->algorithm.block,0,(size_t)s->input_frames*s->factor*s->channels*sizeof(float));
    return ORPHEUS_OK;
}
int orpheus_builtin_downrate_prepare(orpheus_builtin_downrate_State* s,const OrpheusConfig* config) {
    (void)config;
    s->algorithm.block=calloc((size_t)s->input_frames*s->factor*s->channels,sizeof(float));
    if(!s->algorithm.block) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return orpheus_builtin_downrate_reset(s);
}
int orpheus_builtin_downrate_destroy(orpheus_builtin_downrate_State* s) {
    free(s->algorithm.block);s->algorithm.block=NULL;
    return ORPHEUS_OK;
}
int orpheus_builtin_downrate_set_parameter(orpheus_builtin_downrate_State* s,
    const char* key,const OrpheusValue* value) {
    (void)s;(void)key;(void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}
int orpheus_builtin_downrate_algorithm(orpheus_builtin_downrate_State* s,
    orpheus_builtin_downrate_IO* io,const OrpheusProcessContext* ctx) {
    if(!ctx->frame_count) return ORPHEUS_OK;
    if(!s->algorithm.block) return ORPHEUS_ERR_PROCESSING;
    uint32_t frames=(uint32_t)s->input_frames,total=frames*(uint32_t)s->factor;
    size_t bytes=(size_t)total*s->channels*sizeof(float),input_bytes=(size_t)frames*s->channels*sizeof(float);
    if(ctx->trigger_input==io->in.input_index) {
        if(!io->in.buffer || io->in.buffer->frame_count!=frames ||
           overlap(io->in.samples,input_bytes,s,sizeof(*s)) ||
           overlap(io->in.samples,input_bytes,s->algorithm.block,bytes))
            return ORPHEUS_ERR_INVALID_ARG;
        if(s->available_frames>(int32_t)(total-frames)) {
            if(s->overflows<INT_MAX) ++s->overflows;
            return ORPHEUS_ERR_PROCESSING;
        }
        if((!s->available_frames && ctx->frame_index>UINT64_MAX-total) ||
           (s->available_frames && (ctx->epoch!=s->algorithm.epoch ||
                ctx->frame_index!=s->algorithm.frame_index+(uint32_t)s->available_frames ||
                (ctx->timeline_flags&ORPHEUS_TIMELINE_DISCONTINUITY))) ||
           ((ctx->timeline_flags&ORPHEUS_TIMELINE_EOS) && s->available_frames+(int32_t)frames!=(int32_t)total))
            return ORPHEUS_ERR_INVALID_ARG;
        memcpy(s->algorithm.block+(size_t)s->available_frames*s->channels,io->in.samples,input_bytes);
        if(!s->available_frames) {
            s->algorithm.frame_index=ctx->frame_index;s->algorithm.epoch=ctx->epoch;
            s->algorithm.timeline_flags=ctx->timeline_flags;
        } else s->algorithm.timeline_flags|=ctx->timeline_flags;
        s->available_frames+=(int32_t)frames;
        return ORPHEUS_OK;
    }
    if(ctx->trigger_input!=io->tick.input_index || !io->out.buffer)
        return ORPHEUS_ERR_INVALID_ARG;
    if(overlap(io->out.samples,bytes,s,sizeof(*s)) ||
       overlap(io->out.samples,bytes,s->algorithm.block,bytes)) return ORPHEUS_ERR_INVALID_ARG;
    if(s->available_frames!=(int32_t)total) {
        if(s->underflows<INT_MAX) ++s->underflows;
        return ORPHEUS_ERR_PROCESSING;
    }
    memcpy(io->out.samples,s->algorithm.block,bytes);
    io->out.delivery.frame_index=s->algorithm.frame_index;
    io->out.delivery.epoch=s->algorithm.epoch;
    io->out.delivery.timeline_flags=s->algorithm.timeline_flags;
    s->available_frames=0;
    return orpheus_builtin_downrate_emit_out(ctx,&io->out);
}
