#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

#define TWO_PI (2.0f*3.14159265358979f)

int orpheus_builtin_signal_gen_reset(orpheus_builtin_signal_gen_State* s) {
    s->algorithm.phase=0.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_signal_gen_prepare(orpheus_builtin_signal_gen_State* s,const OrpheusConfig* config) {
    (void)config;
    return orpheus_builtin_signal_gen_reset(s);
}

int orpheus_builtin_signal_gen_set_parameter(orpheus_builtin_signal_gen_State* s,
    const char* key,const OrpheusValue* value) {
    (void)s;(void)value;
    return !strcmp(key,"amplitude") ? ORPHEUS_OK : ORPHEUS_ERR_UNSUPPORTED;
}

int orpheus_builtin_signal_gen_algorithm(orpheus_builtin_signal_gen_State* s,
    orpheus_builtin_signal_gen_IO* io,const OrpheusProcessContext* ctx) {
    if(!io->out.buffer || !ctx->sample_rate) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    uintptr_t output=(uintptr_t)io->out.samples,state=(uintptr_t)s;
    if(bytes && (output>=state ? output-state<sizeof(*s) : state-output<bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    float step=TWO_PI*s->frequency/ctx->sample_rate;
    for(uint32_t n=0;n<ctx->frame_count;n++) {
        float sample=s->amplitude*sinf(s->algorithm.phase);
        s->algorithm.phase+=step;
        if(s->algorithm.phase>TWO_PI) s->algorithm.phase-=TWO_PI;
        for(int32_t c=0;c<s->channels;c++) io->out.samples[(size_t)n*s->channels+c]=sample;
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_signal_gen_emit_out(ctx,&io->out);
}
