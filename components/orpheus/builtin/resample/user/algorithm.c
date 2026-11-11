#include "component_state.h"
#include "component_api.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_resample_normalize(orpheus_builtin_resample_Parameters* p,const char* key) {
    uint64_t input=(uint64_t)p->output_frames*(uint32_t)p->factor;
    uint64_t count=(uint64_t)p->output_frames*(uint32_t)p->channels;
    if(p->input_frames<1 || p->phase>=p->factor || input%(uint32_t)p->input_frames ||
       count>SIZE_MAX/sizeof(float) ||
       (!key && (p->available_frames || p->underflows || p->overflows)))
        return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
int orpheus_builtin_resample_reset(orpheus_builtin_resample_State* s) {
    s->available_frames=0;s->underflows=0;s->overflows=0;
    memset(s->algorithm.acc,0,sizeof(s->algorithm.acc));
    s->algorithm.pending=0;s->algorithm.frame_index=0;s->algorithm.epoch=0;
    s->algorithm.timeline_flags=0;
    if(s->algorithm.block)
        memset(s->algorithm.block,0,(size_t)s->output_frames*s->channels*sizeof(float));
    return ORPHEUS_OK;
}
int orpheus_builtin_resample_prepare(orpheus_builtin_resample_State* s,const OrpheusConfig* config) {
    (void)config;
    s->algorithm.block=calloc((size_t)s->output_frames*s->channels,sizeof(float));
    if(!s->algorithm.block) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return orpheus_builtin_resample_reset(s);
}
int orpheus_builtin_resample_destroy(orpheus_builtin_resample_State* s) {
    free(s->algorithm.block);s->algorithm.block=NULL;
    return ORPHEUS_OK;
}
int orpheus_builtin_resample_set_parameter(orpheus_builtin_resample_State* s,
    const char* key,const OrpheusValue* value) {
    (void)s;(void)key;(void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}
int orpheus_builtin_resample_algorithm(orpheus_builtin_resample_State* s,
    orpheus_builtin_resample_IO* io,const OrpheusProcessContext* ctx) {
    if(!ctx->frame_count) return ORPHEUS_OK;
    if(!s->algorithm.block) return ORPHEUS_ERR_PROCESSING;
    uint32_t frames=(uint32_t)s->input_frames,factor=(uint32_t)s->factor;
    uint32_t total=(uint32_t)s->output_frames*factor;
    uint32_t consumed=(uint32_t)s->available_frames*factor+s->algorithm.pending;
    size_t bytes=(size_t)s->output_frames*s->channels*sizeof(float);
    if(ctx->trigger_input==io->in.input_index) {
        size_t input_bytes=(size_t)frames*s->channels*sizeof(float);
        if(!io->in.buffer || io->in.buffer->frame_count!=frames ||
           overlap(io->in.samples,input_bytes,s,sizeof(*s)) ||
           overlap(io->in.samples,input_bytes,s->algorithm.block,bytes))
            return ORPHEUS_ERR_INVALID_ARG;
        if(consumed>total-frames) {
            if(s->overflows<INT_MAX) ++s->overflows;
            return ORPHEUS_ERR_PROCESSING;
        }
        if((!consumed && (ctx->frame_index%factor || ctx->frame_index>UINT64_MAX-total)) ||
           (consumed && (ctx->epoch!=s->algorithm.epoch ||
                ctx->frame_index!=s->algorithm.frame_index+consumed ||
                (ctx->timeline_flags&ORPHEUS_TIMELINE_DISCONTINUITY))) ||
           ((ctx->timeline_flags&ORPHEUS_TIMELINE_EOS) && consumed+frames!=total))
            return ORPHEUS_ERR_INVALID_ARG;
        if(!consumed) {
            s->algorithm.frame_index=ctx->frame_index;s->algorithm.epoch=ctx->epoch;
            s->algorithm.timeline_flags=ctx->timeline_flags;
        } else s->algorithm.timeline_flags|=ctx->timeline_flags;
        for(uint32_t f=0;f<frames;++f) {
            if(!s->mode) {
                for(int32_t c=0;c<s->channels;++c)
                    s->algorithm.acc[c]+=io->in.samples[(size_t)f*s->channels+c];
            } else if(s->algorithm.pending==(uint32_t)s->phase) {
                memcpy(s->algorithm.acc,io->in.samples+(size_t)f*s->channels,
                    (size_t)s->channels*sizeof(float));
            }
            if(++s->algorithm.pending==factor) {
                float* dst=s->algorithm.block+(size_t)s->available_frames*s->channels;
                if(!s->mode) {
                    for(int32_t c=0;c<s->channels;++c) dst[c]=s->algorithm.acc[c]/(float)factor;
                    memset(s->algorithm.acc,0,sizeof(s->algorithm.acc));
                } else memcpy(dst,s->algorithm.acc,(size_t)s->channels*sizeof(float));
                s->algorithm.pending=0;++s->available_frames;
            }
        }
        return ORPHEUS_OK;
    }
    if(ctx->trigger_input!=io->tick.input_index || !io->out.buffer)
        return ORPHEUS_ERR_INVALID_ARG;
    if(overlap(io->out.samples,bytes,s,sizeof(*s)) ||
       overlap(io->out.samples,bytes,s->algorithm.block,bytes)) return ORPHEUS_ERR_INVALID_ARG;
    if(s->available_frames!=s->output_frames || s->algorithm.pending) {
        if(s->underflows<INT_MAX) ++s->underflows;
        return ORPHEUS_ERR_PROCESSING;
    }
    memcpy(io->out.samples,s->algorithm.block,bytes);
    io->out.delivery.frame_index=s->algorithm.frame_index/factor;
    io->out.delivery.epoch=s->algorithm.epoch;
    io->out.delivery.timeline_flags=s->algorithm.timeline_flags;
    s->available_frames=0;
    return orpheus_builtin_resample_emit_out(ctx,&io->out);
}
