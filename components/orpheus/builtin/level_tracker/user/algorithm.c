#include "component_state.h"
#include "component_api.h"
#include <math.h>

int orpheus_builtin_level_tracker_validate(const orpheus_builtin_level_tracker_Parameters* p) {
    return p->min_db>p->max_db || p->hold_calls<0 || p->hold_margin_db<0 || p->volume_epsilon<=0 ?
        ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

int orpheus_builtin_level_tracker_reset(orpheus_builtin_level_tracker_State* s) {
    s->algorithm.initialized=false;
    s->algorithm.hold_count=0;
    s->holding=true;
    s->envelope_db=0;
    s->level_db_spl=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_level_tracker_prepare(orpheus_builtin_level_tracker_State* s,const OrpheusConfig* config) {
    (void)config;
    return orpheus_builtin_level_tracker_reset(s);
}

static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}

int orpheus_builtin_level_tracker_algorithm(orpheus_builtin_level_tracker_State* s,
    orpheus_builtin_level_tracker_IO* io,const OrpheusProcessContext* ctx) {
    if(io->volume.buffer && io->volume_gain.buffer) return ORPHEUS_ERR_INVALID_ARG;
    if(!ctx->frame_count) return ORPHEUS_OK;
    const OrpheusBuffer* inputs[]={io->rms.buffer,io->peak.buffer,io->volume.buffer,
        io->fast_trigger.buffer,io->volume_gain.buffer};
    float* outputs[]={io->level.samples,io->envelope.samples};
    for(unsigned o=0;o<2;o++) if(outputs[o]) {
        if(overlap(outputs[o],sizeof(float),s,sizeof(*s))) return ORPHEUS_ERR_INVALID_ARG;
        for(unsigned i=0;i<5;i++) if(inputs[i] &&
            overlap(outputs[o],sizeof(float),inputs[i]->data,sizeof(float))) return ORPHEUS_ERR_INVALID_ARG;
        if(o && outputs[0] && overlap(outputs[o],sizeof(float),outputs[0],sizeof(float)))
            return ORPHEUS_ERR_INVALID_ARG;
    }
    for(unsigned i=0;i<5;i++) if(inputs[i] && inputs[i]->frame_count!=1)
        return ORPHEUS_ERR_INVALID_ARG;
    float rms=io->rms.buffer ? io->rms.samples[0] : s->rms_db;
    float peak=io->peak.buffer ? io->peak.samples[0] : s->peak_db;
    float volume=io->volume.buffer ? io->volume.samples[0] : s->volume_db;
    float trigger=io->fast_trigger.buffer ? io->fast_trigger.samples[0] : s->trigger;
    if(io->volume_gain.buffer) volume=log10f(io->volume_gain.samples[0]+s->volume_epsilon)*20;
    if(!s->algorithm.initialized) {
        s->algorithm.initialized=true;
        s->envelope_db=-fabsf(s->min_db);
    }
    s->level_db_spl=0;
    if(s->mode>=0) {
        float input=s->mode>0 ? peak : rms;
        float delta=s->envelope_db-input;
        if(delta<=0) {
            s->envelope_db=input;
            s->algorithm.hold_count=0;
            s->holding=true;
        } else {
            float coefficient=s->slow_decay_db;
            if(delta>=s->hold_margin_db) {
                s->algorithm.hold_count++;
                if(s->algorithm.hold_count>s->hold_calls) s->holding=false;
            } else s->algorithm.hold_count=0;
            if(!s->holding) coefficient=s->fast_decay_db;
            if(trigger>s->trigger_threshold) coefficient=s->fast_multiplier*s->fast_decay_db;
            s->envelope_db+=coefficient;
        }
        s->envelope_db=fmaxf(s->min_db,s->envelope_db);
        s->envelope_db=fminf(s->max_db,s->envelope_db);
        s->level_db_spl=fmaxf(0,(s->envelope_db+s->prediction_db_spl)+volume);
    }
    if(io->level.buffer) {
        io->level.samples[0]=s->level_db_spl;
        io->level.buffer->frame_count=1;
    }
    if(io->envelope.buffer) {
        io->envelope.samples[0]=s->envelope_db;
        io->envelope.buffer->frame_count=1;
    }
    int rc=io->level.buffer ? orpheus_builtin_level_tracker_emit_level(ctx,&io->level) : ORPHEUS_OK;
    if(rc) return rc;
    return io->envelope.buffer ? orpheus_builtin_level_tracker_emit_envelope(ctx,&io->envelope) : ORPHEUS_OK;
}
