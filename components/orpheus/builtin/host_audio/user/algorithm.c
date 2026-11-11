#include "component_state.h"
#include "component_api.h"
#include <limits.h>
#include <string.h>

int orpheus_builtin_host_audio_normalize(orpheus_builtin_host_audio_Parameters* p,const char* key) {
    if(!key && (p->ready || p->valid || p->missing_blocks)) return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
int orpheus_builtin_host_audio_reset(orpheus_builtin_host_audio_State* s) {
    memset(s->algorithm.data,0,sizeof(s->algorithm.data));
    s->ready=false;s->valid=false;s->missing_blocks=0;
    return ORPHEUS_OK;
}
int orpheus_builtin_host_audio_prepare(orpheus_builtin_host_audio_State* s,const OrpheusConfig* config) {
    (void)config;
    return orpheus_builtin_host_audio_reset(s);
}
int orpheus_builtin_host_audio_set_parameter(orpheus_builtin_host_audio_State* s,
    const char* key,const OrpheusValue* value) {
    (void)value;
    return !strcmp(key,"ready") && !s->mode ? ORPHEUS_OK : ORPHEUS_ERR_UNSUPPORTED;
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}
int orpheus_builtin_host_audio_algorithm(orpheus_builtin_host_audio_State* s,
    orpheus_builtin_host_audio_IO* io,const OrpheusProcessContext* ctx) {
    if(!ctx->inputs || !ctx->input_count || !ctx->outputs || !ctx->output_count)
        return ORPHEUS_ERR_INVALID_ARG;
    if(!ctx->frame_count) return ORPHEUS_OK;
    uint32_t frames=s->_orpheus_config_frames;
    if(ctx->frame_count!=frames || (io->in.buffer && io->in.buffer->frame_count!=frames))
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)frames*(uint32_t)s->channels*sizeof(float);
    if((io->in.buffer && (io->in.buffer->frame_capacity<frames ||
                         overlap(io->in.samples,bytes,s,sizeof(*s)))) ||
       (io->out.buffer && overlap(io->out.samples,bytes,s,sizeof(*s))) ||
       (io->in.buffer && io->out.buffer && overlap(io->in.samples,bytes,io->out.samples,bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    if(!s->mode) {
        if(io->in.buffer && s->ready) return ORPHEUS_ERR_INVALID_ARG;
        if(io->in.buffer) memcpy(s->algorithm.data,io->in.samples,bytes);
        else if(!s->ready) {
            if(s->missing_blocks<INT_MAX) ++s->missing_blocks;
            return ORPHEUS_ERR_PROCESSING;
        }
        if(io->out.buffer) memcpy(io->out.samples,s->algorithm.data,bytes);
        s->ready=false;
    } else {
        if(!io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
        memcpy(s->algorithm.data,io->in.samples,bytes);
        s->valid=true;
        if(io->out.buffer) memcpy(io->out.samples,io->in.samples,bytes);
    }
    return orpheus_builtin_host_audio_emit_out(ctx,&io->out);
}
