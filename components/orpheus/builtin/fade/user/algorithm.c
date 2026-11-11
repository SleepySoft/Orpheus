#include "component_state.h"
#include "component_api.h"
#include "orpheus_gain_array.h"
#include <limits.h>
#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f
static void targets(float fade,float* front,float* back) {
    if(fade>=0) {*front=1;*back=1.0f-fade;}
    else {*front=1.0f+fade;*back=1;}
}
int orpheus_builtin_fade_validate(const orpheus_builtin_fade_Parameters* p) {
    if(!p->execution_mode) return ORPHEUS_OK;
    int32_t integers[32];
    float values[4];
    return orpheus_parse_gain_array(p->chan_map,integers,p->channels,true,-1,1) ||
        orpheus_parse_gain_array(p->current_gains,values,4,false,0,0) ||
        orpheus_parse_gain_array(p->target_gains,values,4,false,0,0) ||
        orpheus_parse_gain_array(p->coefficients,values,4,false,0,0) ||
        orpheus_parse_gain_array(p->remaining_blocks,integers,4,true,INT32_MIN,INT32_MAX) ?
            ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}
int orpheus_builtin_fade_reset(orpheus_builtin_fade_State* s) {
    if(!s->execution_mode) memset(s->algorithm.z,0,sizeof(s->algorithm.z));
    return ORPHEUS_OK;
}
int orpheus_builtin_fade_prepare(orpheus_builtin_fade_State* s,const OrpheusConfig* config) {
    if(s->execution_mode) {
        return orpheus_parse_gain_array(s->chan_map,s->algorithm.chan_map,s->channels,true,-1,1) ||
            orpheus_parse_gain_array(s->current_gains,s->algorithm.current_gains,4,false,0,0) ||
            orpheus_parse_gain_array(s->target_gains,s->algorithm.target_gains,4,false,0,0) ||
            orpheus_parse_gain_array(s->coefficients,s->algorithm.coefficients,4,false,0,0) ||
            orpheus_parse_gain_array(s->remaining_blocks,s->algorithm.remaining_blocks,4,true,INT32_MIN,INT32_MAX) ?
                ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
    }
    if(s->front_channels>=s->channels) s->front_channels=s->channels-1;
    s->algorithm.a1=config->sample_rate>0 ?
        expf(-2.0f*PI_F*s->crossover_hz/(float)config->sample_rate) : 0;
    targets(s->fade,&s->algorithm.target_front,&s->algorithm.target_back);
    s->algorithm.front_gain=s->algorithm.target_front;s->algorithm.back_gain=s->algorithm.target_back;
    if(s->ramp_ms<=0 || config->sample_rate==0) s->algorithm.smoothing_coeff=1;
    else {
        s->algorithm.smoothing_coeff=1.0f-expf(-1.0f/((s->ramp_ms/1000.0f)*(float)config->sample_rate));
        if(s->algorithm.smoothing_coeff>1) s->algorithm.smoothing_coeff=1;
    }
    return orpheus_builtin_fade_reset(s);
}
int orpheus_builtin_fade_set_parameter(orpheus_builtin_fade_State* s,
    const char* key,const OrpheusValue* value) {
    if(!strcmp(key,"freeze")) return s->execution_mode ? ORPHEUS_OK : ORPHEUS_ERR_UNSUPPORTED;
    if(strcmp(key,"fade")) return ORPHEUS_ERR_NOT_FOUND;
    if(s->execution_mode) return ORPHEUS_ERR_UNSUPPORTED;
    targets(value->value.f32,&s->algorithm.target_front,&s->algorithm.target_back);
    return ORPHEUS_OK;
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}
int orpheus_builtin_fade_algorithm(orpheus_builtin_fade_State* s,
    orpheus_builtin_fade_IO* io,const OrpheusProcessContext* ctx) {
    if(!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if(overlap(io->in.samples,bytes,s,sizeof(*s)) || overlap(io->out.samples,bytes,s,sizeof(*s)) ||
       (io->in.samples!=io->out.samples && overlap(io->in.samples,bytes,io->out.samples,bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames=ctx->frame_count,channels=(uint32_t)s->channels;
    if(s->execution_mode) {
        if(io->lpf_count!=1 || !io->lpf[0].buffer || io->lpf[0].buffer->frame_count<frames)
            return ORPHEUS_ERR_INVALID_ARG;
        const float* low=io->lpf[0].samples;
        if(overlap(low,bytes,s,sizeof(*s)) ||
           (low!=io->out.samples && overlap(low,bytes,io->out.samples,bytes)))
            return ORPHEUS_ERR_INVALID_ARG;
        for(unsigned c=0;c<channels;c++)
            if(s->algorithm.chan_map[c]<-1 || s->algorithm.chan_map[c]>1) return ORPHEUS_ERR_INVALID_ARG;
        for(unsigned r=0;r<4;r++)
            if(!isfinite(s->algorithm.current_gains[r]) || s->algorithm.current_gains[r]<0 ||
               !isfinite(s->algorithm.target_gains[r]) || s->algorithm.target_gains[r]<0 ||
               !isfinite(s->algorithm.coefficients[r]) || s->algorithm.coefficients[r]<0)
                return ORPHEUS_ERR_INVALID_ARG;
        if(frames) {
            float starts[4],coefficients[4]={1,1,1,1},gains[4];
            memcpy(starts,s->algorithm.current_gains,sizeof(starts));
            for(unsigned r=0;r<2;r++) if(!s->freeze) {
                if(!s->algorithm.remaining_blocks[r]) {
                    starts[r]=s->algorithm.target_gains[r];starts[r+2]=s->algorithm.target_gains[r+2];
                } else {
                    coefficients[r]=s->algorithm.coefficients[r];coefficients[r+2]=s->algorithm.coefficients[r+2];
                    if(s->algorithm.remaining_blocks[r]>INT32_MIN) s->algorithm.remaining_blocks[r]--;
                }
            }
            for(uint32_t n=0;n<frames;n++) {
                for(unsigned r=0;r<4;r++)
                    gains[r]=starts[r]*(coefficients[r]==1 ? 1 : powf(coefficients[r],(float)(n+1)));
                for(uint32_t c=0;c<channels;c++) {
                    size_t i=(size_t)n*channels+c;
                    int32_t r=s->algorithm.chan_map[c];
                    if(r<0) io->out.samples[i]=0;
                    else {
                        float high=io->in.samples[i]-low[i];
                        io->out.samples[i]=low[i]*gains[r+2]+high*gains[r];
                    }
                }
            }
            memcpy(s->algorithm.current_gains,gains,sizeof(gains));
        }
    } else {
        float b0=1.0f-s->algorithm.a1;
        for(uint32_t n=0;n<frames;n++) {
            s->algorithm.front_gain+=s->algorithm.smoothing_coeff*(s->algorithm.target_front-s->algorithm.front_gain);
            s->algorithm.back_gain+=s->algorithm.smoothing_coeff*(s->algorithm.target_back-s->algorithm.back_gain);
            for(uint32_t c=0;c<channels;c++) {
                size_t i=(size_t)n*channels+c;
                float x=io->in.samples[i],low=b0*x+s->algorithm.a1*s->algorithm.z[c];
                s->algorithm.z[c]=low;
                float gain=c<(uint32_t)s->front_channels ? s->algorithm.front_gain : s->algorithm.back_gain;
                io->out.samples[i]=low+gain*(x-low);
            }
        }
    }
    io->out.buffer->frame_count=frames;
    return orpheus_builtin_fade_emit_out(ctx,&io->out);
}
