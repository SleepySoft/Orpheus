#include "component_state.h"
#include "component_api.h"
#include <string.h>

static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}

static int publish(const orpheus_builtin_one_pole_envelope_IO* io,const OrpheusProcessContext* ctx) {
    int rc=io->out.buffer ? orpheus_builtin_one_pole_envelope_emit_out(ctx,&io->out) : ORPHEUS_OK;
    if(rc) return rc;
    rc=io->gains.buffer ? orpheus_builtin_one_pole_envelope_emit_gains(ctx,&io->gains) : ORPHEUS_OK;
    if(rc) return rc;
    return io->audio.buffer ? orpheus_builtin_one_pole_envelope_emit_audio(ctx,&io->audio) : ORPHEUS_OK;
}

int orpheus_builtin_one_pole_envelope_algorithm(orpheus_builtin_one_pole_envelope_State* s,
    orpheus_builtin_one_pole_envelope_IO* io,const OrpheusProcessContext* ctx) {
    if(!ctx->frame_count) return publish(io,ctx);
    if(!io->in.buffer || io->in.buffer->frame_count!=ctx->frame_count ||
        !io->start.buffer || io->start.buffer->frame_count!=1 ||
        !io->target.buffer || io->target.buffer->frame_count!=1 ||
        !io->alpha.buffer || io->alpha.buffer->frame_count!=1 ||
        !io->freeze.buffer || io->freeze.buffer->frame_count!=1) return ORPHEUS_ERR_INVALID_ARG;
    size_t vector_bytes=(size_t)s->channels*sizeof(float),audio_bytes=vector_bytes*ctx->frame_count;
    const void* inputs[]={io->start.samples,io->target.samples,io->alpha.samples,io->freeze.samples,io->in.samples,s};
    const size_t input_bytes[]={vector_bytes,vector_bytes,4,4,audio_bytes,sizeof(*s)};
    void* outputs[]={io->out.samples,io->gains.samples,io->audio.samples};
    const size_t output_bytes[]={audio_bytes,vector_bytes,audio_bytes};
    for(unsigned o=0;o<3;o++) if(outputs[o]) {
        for(unsigned i=0;i<6;i++)
            if(overlap(outputs[o],output_bytes[o],inputs[i],input_bytes[i])) return ORPHEUS_ERR_INVALID_ARG;
        for(unsigned previous=0;previous<o;previous++)
            if(outputs[previous] && overlap(outputs[o],output_bytes[o],outputs[previous],output_bytes[previous]))
                return ORPHEUS_ERR_INVALID_ARG;
    }
    float alpha=io->alpha.samples[0],gains[128];
    int32_t freeze=io->freeze.samples[0];
    memcpy(gains,io->start.samples,vector_bytes);
    for(uint32_t n=0;n<ctx->frame_count;n++) for(int32_t c=0;c<s->channels;c++) {
        if(!freeze) {
            volatile float feed=(1.0f-alpha)*io->target.samples[c],feedback=alpha*gains[c];
            gains[c]=feed+feedback;
        }
        if(io->out.buffer) io->out.samples[(size_t)n*s->channels+c]=gains[c];
    }
    if(io->out.buffer) io->out.buffer->frame_count=ctx->frame_count;
    if(io->gains.buffer) {
        memcpy(io->gains.samples,gains,vector_bytes);
        io->gains.buffer->frame_count=1;
    }
    if(io->audio.buffer) {
        memcpy(io->audio.samples,io->in.samples,audio_bytes);
        io->audio.buffer->frame_count=ctx->frame_count;
    }
    return publish(io,ctx);
}
