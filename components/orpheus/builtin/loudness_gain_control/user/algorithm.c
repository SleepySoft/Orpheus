#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

#define LGC_FLOOR_DB (-120.0f)
#define LGC_EPSILON (1.0e-12f)

static float energy_to_db(float energy) {
    return energy<=LGC_EPSILON ? LGC_FLOOR_DB : 10.0f*log10f(energy);
}
static float block_coeff(float ms,uint32_t frames,double sample_rate) {
    if(ms<=0 || !frames || sample_rate==0) return 1.0f;
    return 1.0f-expf(-(float)frames/(ms*0.001f*(float)sample_rate));
}
int orpheus_builtin_loudness_gain_control_reset(orpheus_builtin_loudness_gain_control_State* s) {
    s->algorithm.detector_energy=0;
    s->gain_db=0;
    s->input_db=LGC_FLOOR_DB;
    s->output_db=LGC_FLOOR_DB;
    return ORPHEUS_OK;
}
int orpheus_builtin_loudness_gain_control_prepare(orpheus_builtin_loudness_gain_control_State* s,
    const OrpheusConfig* config) {
    (void)config;
    return orpheus_builtin_loudness_gain_control_reset(s);
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}
int orpheus_builtin_loudness_gain_control_algorithm(orpheus_builtin_loudness_gain_control_State* s,
    orpheus_builtin_loudness_gain_control_IO* io,const OrpheusProcessContext* ctx) {
    if(!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if(overlap(io->in.samples,bytes,s,sizeof(*s)) || overlap(io->out.samples,bytes,s,sizeof(*s)) ||
       (io->in.samples!=io->out.samples && overlap(io->in.samples,bytes,io->out.samples,bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    float level=s->level>=0 ? s->level : 0;
    float raw_energy=level*level,raw_db=energy_to_db(raw_energy);
    if(s->algorithm.detector_energy<=LGC_EPSILON && raw_energy>LGC_EPSILON) {
        s->algorithm.detector_energy=raw_energy;
    } else {
        float ms=raw_energy>s->algorithm.detector_energy ? s->detector_attack_ms : s->detector_release_ms;
        float coefficient=block_coeff(ms,ctx->frame_count,ctx->sample_rate);
        s->algorithm.detector_energy+=coefficient*(raw_energy-s->algorithm.detector_energy);
    }
    s->input_db=energy_to_db(s->algorithm.detector_energy);
    float desired=s->gain_db;
    if(raw_db>=s->gate_db) {
        float lower=s->min_gain_db<s->max_gain_db ? s->min_gain_db : s->max_gain_db;
        float upper=s->min_gain_db<s->max_gain_db ? s->max_gain_db : s->min_gain_db;
        desired=s->target_db-s->input_db;
        if(desired<lower) desired=lower;
        else if(desired>upper) desired=upper;
    }
    float gain_ms=desired<s->gain_db ? s->gain_attack_ms : s->gain_release_ms;
    float coefficient=block_coeff(gain_ms,ctx->frame_count,ctx->sample_rate);
    s->gain_db+=coefficient*(desired-s->gain_db);
    s->output_db=s->input_db>LGC_FLOOR_DB ? s->input_db+s->gain_db : LGC_FLOOR_DB;
    if(bytes && io->out.samples!=io->in.samples) memcpy(io->out.samples,io->in.samples,bytes);
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_loudness_gain_control_emit_out(ctx,&io->out);
}
