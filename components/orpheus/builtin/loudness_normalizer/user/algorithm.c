#include "component_state.h"
#include "component_api.h"
#include <math.h>

#define LN_FLOOR_DB (-120.0f)
#define LN_EPSILON (1.0e-12f)
static float energy_to_db(float energy) {
    return energy<=LN_EPSILON ? LN_FLOOR_DB : 10.0f*log10f(energy);
}
static float block_coeff(float ms,uint32_t frames,double sample_rate) {
    if(ms<=0 || !frames || sample_rate==0) return 1.0f;
    return 1.0f-expf(-(float)frames/(ms*0.001f*(float)sample_rate));
}
int orpheus_builtin_loudness_normalizer_reset(orpheus_builtin_loudness_normalizer_State* s) {
    s->algorithm.detector_energy=0;
    s->gain_db=0;
    s->input_db=LN_FLOOR_DB;
    s->output_db=LN_FLOOR_DB;
    return ORPHEUS_OK;
}
int orpheus_builtin_loudness_normalizer_prepare(orpheus_builtin_loudness_normalizer_State* s,
    const OrpheusConfig* config) {
    (void)config;
    return orpheus_builtin_loudness_normalizer_reset(s);
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}
int orpheus_builtin_loudness_normalizer_algorithm(orpheus_builtin_loudness_normalizer_State* s,
    orpheus_builtin_loudness_normalizer_IO* io,const OrpheusProcessContext* ctx) {
    if(!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t count=(size_t)ctx->frame_count*s->channels,bytes=count*sizeof(float);
    if(overlap(io->in.samples,bytes,s,sizeof(*s)) || overlap(io->out.samples,bytes,s,sizeof(*s)) ||
       (io->in.samples!=io->out.samples && overlap(io->in.samples,bytes,io->out.samples,bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    if(!count) {
        io->out.buffer->frame_count=0;
        return orpheus_builtin_loudness_normalizer_emit_out(ctx,&io->out);
    }
    float energy=0;
    for(size_t n=0;n<count;n++) energy+=io->in.samples[n]*io->in.samples[n];
    energy/=(float)count;
    float block_db=energy_to_db(energy);
    if(s->algorithm.detector_energy<=LN_EPSILON && energy>LN_EPSILON) {
        s->algorithm.detector_energy=energy;
    } else {
        float ms=energy>s->algorithm.detector_energy ? s->detector_attack_ms : s->detector_release_ms;
        float coefficient=block_coeff(ms,ctx->frame_count,ctx->sample_rate);
        s->algorithm.detector_energy+=coefficient*(energy-s->algorithm.detector_energy);
    }
    s->input_db=energy_to_db(s->algorithm.detector_energy);
    float desired=s->gain_db;
    if(block_db>=s->gate_db) {
        float lower=s->min_gain_db<s->max_gain_db ? s->min_gain_db : s->max_gain_db;
        float upper=s->min_gain_db<s->max_gain_db ? s->max_gain_db : s->min_gain_db;
        desired=s->target_db-s->input_db;
        if(desired<lower) desired=lower;
        else if(desired>upper) desired=upper;
    }
    float old_db=s->gain_db;
    float ms=desired<old_db ? s->gain_attack_ms : s->gain_release_ms;
    float coefficient=block_coeff(ms,ctx->frame_count,ctx->sample_rate);
    s->gain_db+=coefficient*(desired-s->gain_db);
    s->output_db=s->input_db>LN_FLOOR_DB ? s->input_db+s->gain_db : LN_FLOOR_DB;
    float old_linear=powf(10.0f,old_db/20.0f),new_linear=powf(10.0f,s->gain_db/20.0f);
    float step=(new_linear-old_linear)/(float)ctx->frame_count,gain=old_linear;
    for(uint32_t n=0;n<ctx->frame_count;n++) {
        gain+=step;
        for(int32_t c=0;c<s->channels;c++) {
            size_t index=(size_t)n*s->channels+c;
            io->out.samples[index]=io->in.samples[index]*gain;
        }
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_loudness_normalizer_emit_out(ctx,&io->out);
}
