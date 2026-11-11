#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_level_detect_validate(const orpheus_builtin_level_detect_Parameters* p) {
    return p->execution_mode==1 && p->db_epsilon<=0 ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

int orpheus_builtin_level_detect_reset(orpheus_builtin_level_detect_State* s) {
    if(s->execution_mode==1) {
        s->rms_db=log10f(s->db_epsilon)*10;
        s->peak_db=log10f(s->db_epsilon)*20;
        s->level=s->rms_db;
    } else {
        memset(s->algorithm.env,0,sizeof(s->algorithm.env));
        s->level=0.0f;
    }
    return ORPHEUS_OK;
}

static float coefficient(float ms,uint32_t rate) {
    if(!rate) return 1.0f;
    float value=1.0f-expf(-1.0f/((ms/1000.0f)*(float)rate));
    return value>1.0f ? 1.0f : value;
}

int orpheus_builtin_level_detect_prepare(orpheus_builtin_level_detect_State* s,const OrpheusConfig* config) {
    s->algorithm.attack_coeff=coefficient(s->attack_ms,config->sample_rate);
    s->algorithm.release_coeff=coefficient(s->release_ms,config->sample_rate);
    return orpheus_builtin_level_detect_reset(s);
}

int orpheus_builtin_level_detect_set_parameter(orpheus_builtin_level_detect_State* s,
    const char* key,const OrpheusValue* value) {
    (void)value;
    if(s->execution_mode==1) return ORPHEUS_ERR_INVALID_ARG;
    return !strcmp(key,"attack_ms") || !strcmp(key,"release_ms") ? ORPHEUS_OK : ORPHEUS_ERR_NOT_FOUND;
}

static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}

int orpheus_builtin_level_detect_algorithm(orpheus_builtin_level_detect_State* s,
    orpheus_builtin_level_detect_IO* io,const OrpheusProcessContext* ctx) {
    if(!ctx->frame_count) return ORPHEUS_OK;
    if(!io->in.buffer || io->in.buffer->frame_count<ctx->frame_count ||
        (!s->execution_mode && !io->out.buffer)) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    void* outputs[]={io->out.samples,io->rms_count ? io->rms[0].samples : NULL,
        io->peak_count ? io->peak[0].samples : NULL};
    size_t lengths[]={bytes,4,4};
    for(unsigned o=0;o<3;o++) if(outputs[o]) {
        if(overlap(outputs[o],lengths[o],s,sizeof(*s)) ||
            ((o || outputs[o]!=io->in.samples) && overlap(outputs[o],lengths[o],io->in.samples,bytes)))
            return ORPHEUS_ERR_INVALID_ARG;
        for(unsigned prior=0;prior<o;prior++)
            if(outputs[prior] && overlap(outputs[o],lengths[o],outputs[prior],lengths[prior]))
                return ORPHEUS_ERR_INVALID_ARG;
    }
    const float* values=io->in.samples;
    uint32_t frames=ctx->frame_count,channels=(uint32_t)s->channels;
    if(s->execution_mode==1) {
        float powers[32],peaks[32];
        for(uint32_t c=0;c<channels;c++) {
            float power=values[c]*values[c],peak=fabsf(values[c]);
            for(uint32_t n=1;n<frames;n++) {
                float value=values[(size_t)n*channels+c];
                power+=value*value;
                float magnitude=fabsf(value);
                if(peak<magnitude) peak=magnitude;
            }
            powers[c]=power/(float)frames;peaks[c]=peak;
        }
        float power=powers[0],peak=peaks[0];
        for(uint32_t c=1;c<channels;c++) {
            if(power<powers[c]) power=powers[c];
            if(peak<peaks[c]) peak=peaks[c];
        }
        s->rms_db=log10f(power+s->db_epsilon)*10;
        s->peak_db=log10f(peak+s->db_epsilon)*20;
        s->level=s->rms_db;
        if(io->rms[0].buffer) {
            io->rms[0].samples[0]=s->rms_db;io->rms[0].buffer->frame_count=1;
        }
        if(io->peak[0].buffer) {
            io->peak[0].samples[0]=s->peak_db;io->peak[0].buffer->frame_count=1;
        }
    } else {
        float maximum=0.0f;
        if(s->mode==1) {
            float power[32]={0};
            for(uint32_t n=0;n<frames;n++) for(uint32_t c=0;c<channels;c++) {
                float value=values[(size_t)n*channels+c];
                power[c]+=value*value;
            }
            for(uint32_t c=0;c<channels;c++) {
                float rms=sqrtf(power[c]/(float)frames);
                s->algorithm.env[c]+=s->algorithm.attack_coeff*(rms-s->algorithm.env[c]);
                if(s->algorithm.env[c]>maximum) maximum=s->algorithm.env[c];
            }
        } else {
            for(size_t n=0;n<(size_t)frames*channels;n++) {
                uint32_t c=(uint32_t)(n%channels);
                float magnitude=values[n]>=0.0f ? values[n] : -values[n];
                float rate=magnitude>s->algorithm.env[c] ? s->algorithm.attack_coeff : s->algorithm.release_coeff;
                s->algorithm.env[c]+=rate*(magnitude-s->algorithm.env[c]);
                if(s->algorithm.env[c]>maximum) maximum=s->algorithm.env[c];
            }
        }
        s->level=maximum;
    }
    if(io->out.buffer) {
        if(io->out.samples!=values) memcpy(io->out.samples,values,bytes);
        io->out.buffer->frame_count=frames;
        int rc=orpheus_builtin_level_detect_emit_out(ctx,&io->out);
        if(rc) return rc;
    }
    if(s->execution_mode==1) {
        int rc=io->rms[0].buffer ? orpheus_builtin_level_detect_emit_rms(ctx,&io->rms[0]) : ORPHEUS_OK;
        if(rc) return rc;
        return io->peak[0].buffer ? orpheus_builtin_level_detect_emit_peak(ctx,&io->peak[0]) : ORPHEUS_OK;
    }
    return ORPHEUS_OK;
}
