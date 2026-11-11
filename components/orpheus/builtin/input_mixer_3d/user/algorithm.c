#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse_weights(const char* text, float* output, unsigned rows, unsigned cols) {
    while (isspace((unsigned char)*text)) ++text;
    if (!*text) return ORPHEUS_OK;
    unsigned index=0;
    for (;;) {
        char* end;
        float value=(float)strtod(text,&end);
        if (end==text || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        if (output && index<rows*cols) output[(index/cols)*32+index%cols]=value;
        ++index;
        while (isspace((unsigned char)*end)) ++end;
        if (!*end) return ORPHEUS_OK;
        if (*end!=',') return ORPHEUS_ERR_INVALID_ARG;
        text=end+1;
    }
}

int orpheus_builtin_input_mixer_3d_validate(const orpheus_builtin_input_mixer_3d_Parameters* p) {
    return parse_weights(p->weights,NULL,(unsigned)p->output_channels,(unsigned)p->input_channels);
}

int orpheus_builtin_input_mixer_3d_prepare(orpheus_builtin_input_mixer_3d_State* s,
    const OrpheusConfig* config) {
    (void)config;
    memset(s->algorithm.weights,0,sizeof(s->algorithm.weights));
    for (int32_t i=0;i<s->input_channels && i<s->output_channels;++i)
        s->algorithm.weights[i*32+i]=1;
    return parse_weights(s->weights,s->algorithm.weights,(unsigned)s->output_channels,(unsigned)s->input_channels);
}

int orpheus_builtin_input_mixer_3d_reset(orpheus_builtin_input_mixer_3d_State* s) {
    (void)s;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_input_mixer_3d_algorithm(orpheus_builtin_input_mixer_3d_State* s,
    orpheus_builtin_input_mixer_3d_IO* io,const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t input_bytes=(size_t)ctx->frame_count*s->input_channels*sizeof(float);
    size_t output_bytes=(size_t)ctx->frame_count*s->output_channels*sizeof(float);
    if (overlap(io->in.samples,input_bytes,io->out.samples,output_bytes) ||
        overlap(io->in.samples,input_bytes,s,sizeof(*s)) || overlap(io->out.samples,output_bytes,s,sizeof(*s)))
        return ORPHEUS_ERR_INVALID_ARG;
    float gain=powf(10.0f,s->gain_db/20.0f);
    if (!isfinite(gain)) return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t n=0;n<ctx->frame_count;++n) for (int32_t r=0;r<s->output_channels;++r) {
        float value=0;
        for (int32_t c=0;c<s->input_channels;++c)
            value+=s->algorithm.weights[r*32+c]*io->in.samples[(size_t)n*s->input_channels+c];
        io->out.samples[(size_t)n*s->output_channels+r]=value*gain;
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_input_mixer_3d_emit_out(ctx,&io->out);
}
