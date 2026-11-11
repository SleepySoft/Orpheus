#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_sweep_gen_reset(orpheus_builtin_sweep_gen_State* s) {
    s->algorithm.t=s->algorithm.phase=0.0;
    return ORPHEUS_OK;
}

int orpheus_builtin_sweep_gen_prepare(orpheus_builtin_sweep_gen_State* s,const OrpheusConfig* config) {
    (void)config;
    return orpheus_builtin_sweep_gen_reset(s);
}

int orpheus_builtin_sweep_gen_set_parameter(orpheus_builtin_sweep_gen_State* s,
    const char* key,const OrpheusValue* value) {
    (void)s;(void)value;
    return !strcmp(key,"amplitude") ? ORPHEUS_OK : ORPHEUS_ERR_UNSUPPORTED;
}

int orpheus_builtin_sweep_gen_algorithm(orpheus_builtin_sweep_gen_State* s,
    orpheus_builtin_sweep_gen_IO* io,const OrpheusProcessContext* ctx) {
    if(!io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    uintptr_t output=(uintptr_t)io->out.samples,state=(uintptr_t)s;
    if(bytes && (output>=state ? output-state<sizeof(*s) : state-output<bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    double fs=ctx->sample_rate ? (double)ctx->sample_rate : 48000.0;
    double dt=1.0/fs,duration=s->duration_s,start=s->start_freq,end=s->end_freq;
    double ratio=end/start,frequency=0.0;
    for(uint32_t n=0;n<ctx->frame_count;n++) {
        double t=s->algorithm.t;
        if(t>=duration) frequency=0.0;
        else if(s->log_scale) frequency=start*pow(ratio,t/duration);
        else frequency=start+(end-start)*(t/duration);
        float sample=frequency>0.0 ? (float)(s->amplitude*sin(s->algorithm.phase)) : 0.0f;
        for(int32_t c=0;c<s->channels;c++) io->out.samples[(size_t)n*s->channels+c]=sample;
        s->algorithm.phase+=2.0*3.14159265358979323846*frequency*dt;
        if(s->algorithm.phase>6.28318530717958647692*64.0)
            s->algorithm.phase-=6.28318530717958647692*64.0;
        s->algorithm.t+=dt;
    }
    s->current_freq=(float)frequency;
    s->progress=(float)(s->algorithm.t/duration);
    if(s->progress>1.0f) s->progress=1.0f;
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_sweep_gen_emit_out(ctx,&io->out);
}
