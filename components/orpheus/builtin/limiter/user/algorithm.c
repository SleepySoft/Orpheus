#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float ms_to_coeff(float ms,double rate) {
    if(ms<=0 || rate==0) return 1;
    float value=1.0f-expf(-1.0f/((ms/1000.0f)*(float)rate));
    return value>1 ? 1 : value;
}
static int parse_feedback(const char* cursor,float* values,unsigned count,float fallback) {
    if(!*cursor) {
        for(unsigned n=0;n<count;n++) values[n]=fallback;
        return ORPHEUS_OK;
    }
    for(unsigned n=0;n<count;n++) {
        char* end;
        float value=strtof(cursor,&end);
        if(end==cursor || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        while(isspace((unsigned char)*end)) end++;
        if(n+1<count ? *end!=',' : *end!='\0') return ORPHEUS_ERR_INVALID_ARG;
        values[n]=value;cursor=n+1<count ? end+1 : end;
    }
    return ORPHEUS_OK;
}
static int parse_repeated(const char* cursor,float* values,unsigned channels,float fallback) {
    float parsed[32];
    unsigned count=0;
    while(*cursor) {
        if(count==32) return ORPHEUS_ERR_INVALID_ARG;
        char* end;
        float value=(float)strtod(cursor,&end);
        if(end==cursor || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        parsed[count++]=value;cursor=end;
        while(*cursor==',' || isspace((unsigned char)*cursor)) cursor++;
    }
    float last=fallback;
    for(unsigned c=0;c<channels;c++) {
        if(c<count) last=parsed[c];
        values[c]=last;
    }
    return ORPHEUS_OK;
}
int orpheus_builtin_limiter_validate(const orpheus_builtin_limiter_Parameters* p) {
    float values[64];
    if(!strcmp(p->mode,"shared")) return ORPHEUS_OK;
    if(!strcmp(p->mode,"per_channel")) {
        return parse_repeated(p->attack_coeffs,values,p->channels,.0237f) ||
            parse_repeated(p->release_coeffs,values,p->channels,1.00024f) ||
            parse_repeated(p->k1,values,p->channels,.01185f) ||
            parse_repeated(p->max_attack,values,p->channels,.31623f) ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
    }
    if(strcmp(p->mode,"feedback")) return ORPHEUS_ERR_INVALID_ARG;
    unsigned count=2*(unsigned)p->channels;
    return parse_feedback(p->feedback_attack,values,count,.0237f) ||
        parse_feedback(p->feedback_release,values,count,1.00024f) ||
        parse_feedback(p->feedback_k1,values,count,.01185f) ||
        parse_feedback(p->feedback_min_factor,values,count,.31623f) ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}
int orpheus_builtin_limiter_reset(orpheus_builtin_limiter_State* s) {
    for(unsigned c=0;c<32;c++) s->algorithm.env[c]=s->algorithm.mode==2 ? 1 : 0;
    s->gain=1;s->active_mask=0;
    return ORPHEUS_OK;
}
int orpheus_builtin_limiter_prepare(orpheus_builtin_limiter_State* s,const OrpheusConfig* config) {
    s->algorithm.mode=!strcmp(s->mode,"feedback") ? 2 : !strcmp(s->mode,"per_channel") ? 1 : 0;
    s->algorithm.threshold_linear=powf(10.0f,s->threshold_db/20.0f);
    if(s->algorithm.mode==2) {
        unsigned count=2*(unsigned)s->channels;
        if(parse_feedback(s->feedback_attack,s->algorithm.feedback_attack,count,.0237f) ||
           parse_feedback(s->feedback_release,s->algorithm.feedback_release,count,1.00024f) ||
           parse_feedback(s->feedback_k1,s->algorithm.feedback_k1,count,.01185f) ||
           parse_feedback(s->feedback_min_factor,s->algorithm.feedback_min_factor,count,.31623f))
            return ORPHEUS_ERR_INVALID_ARG;
    } else if(s->algorithm.mode==1) {
        if(parse_repeated(s->attack_coeffs,s->algorithm.attack_coeff_per_channel,s->channels,.0237f) ||
           parse_repeated(s->release_coeffs,s->algorithm.release_coeff_per_channel,s->channels,1.00024f) ||
           parse_repeated(s->k1,s->algorithm.k1,s->channels,.01185f) ||
           parse_repeated(s->max_attack,s->algorithm.max_attack,s->channels,.31623f))
            return ORPHEUS_ERR_INVALID_ARG;
    } else {
        s->algorithm.attack_coeff=ms_to_coeff(s->attack_ms,config->sample_rate);
        s->algorithm.release_coeff=ms_to_coeff(s->release_ms,config->sample_rate);
    }
    return orpheus_builtin_limiter_reset(s);
}
int orpheus_builtin_limiter_set_parameter(orpheus_builtin_limiter_State* s,
    const char* key,const OrpheusValue* value) {
    if(!strcmp(key,"parameter_set") || !strcmp(key,"disabled"))
        return s->algorithm.mode==2 ? ORPHEUS_OK : ORPHEUS_ERR_UNSUPPORTED;
    if(s->algorithm.mode==2) return ORPHEUS_ERR_UNSUPPORTED;
    if(!strcmp(key,"threshold_db")) return ORPHEUS_OK;
    if(!strcmp(key,"attack_ms")) {
        s->algorithm.attack_coeff=ms_to_coeff(value->value.f32,48000);
        return ORPHEUS_OK;
    }
    if(!strcmp(key,"release_ms")) {
        s->algorithm.release_coeff=ms_to_coeff(value->value.f32,48000);
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}
int orpheus_builtin_limiter_algorithm(orpheus_builtin_limiter_State* s,
    orpheus_builtin_limiter_IO* io,const OrpheusProcessContext* ctx) {
    if(!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t count=(size_t)ctx->frame_count*s->channels,bytes=count*sizeof(float);
    if(overlap(io->in.samples,bytes,s,sizeof(*s)) || overlap(io->out.samples,bytes,s,sizeof(*s)) ||
       (io->in.samples!=io->out.samples && overlap(io->in.samples,bytes,io->out.samples,bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t channels=(uint32_t)s->channels,frames=ctx->frame_count;
    if(s->algorithm.mode==2) {
        if(s->parameter_set<1 || s->parameter_set>2) return ORPHEUS_ERR_INVALID_ARG;
        if(frames) {
            if(s->disabled) {
                if(io->out.samples!=io->in.samples) memcpy(io->out.samples,io->in.samples,bytes);
                s->active_mask=0;
            } else {
                unsigned base=(unsigned)(s->parameter_set-1)*channels;
                for(unsigned c=0;c<channels;c++) {
                    unsigned i=base+c;
                    if(!isfinite(s->algorithm.feedback_attack[i]) || !isfinite(s->algorithm.feedback_release[i]) ||
                       !isfinite(s->algorithm.feedback_k1[i]) || !isfinite(s->algorithm.feedback_min_factor[i]))
                        return ORPHEUS_ERR_INVALID_ARG;
                }
                uint32_t mask=0;
                for(unsigned c=0;c<channels;c++) {
                    unsigned i=base+c;
                    float gain=s->algorithm.env[c],total_in=0,total_out=0,attack=s->algorithm.feedback_attack[i];
                    for(unsigned n=0;n<frames;n++) {
                        size_t index=(size_t)n*channels+c;
                        float x=io->in.samples[index],magnitude=fabsf(x);
                        total_in+=magnitude;
                        gain=fminf(fmaxf((attack+1.0f)-fmaxf(s->algorithm.feedback_k1[i]*gain*magnitude,attack),
                            s->algorithm.feedback_min_factor[i])*(gain*s->algorithm.feedback_release[i]),1.0f);
                        float y=x*gain;
                        total_out+=fabsf(y);io->out.samples[index]=y;
                    }
                    s->algorithm.env[c]=gain;
                    if(total_out<total_in) mask|=(uint32_t)1<<c;
                }
                memcpy(&s->active_mask,&mask,sizeof(mask));
                s->gain=s->algorithm.env[channels-1];
            }
        }
    } else {
        s->algorithm.threshold_linear=powf(10.0f,s->threshold_db/20.0f);
        if(!s->algorithm.mode) {
            for(size_t n=0;n<count;n++) {
                float x=io->in.samples[n],magnitude=x>=0 ? x : -x;
                float coefficient=magnitude>s->algorithm.env[0] ? s->algorithm.attack_coeff : s->algorithm.release_coeff;
                s->algorithm.env[0]+=coefficient*(magnitude-s->algorithm.env[0]);
                float gain=s->algorithm.env[0]>s->algorithm.threshold_linear && s->algorithm.env[0]>0 ?
                    s->algorithm.threshold_linear/s->algorithm.env[0] : 1;
                s->gain=gain;io->out.samples[n]=x*gain;
            }
        } else {
            for(uint32_t n=0;n<frames;n++) for(uint32_t c=0;c<channels;c++) {
                size_t index=(size_t)n*channels+c;
                float x=io->in.samples[index],magnitude=x>=0 ? x : -x;
                float coefficient=magnitude>s->algorithm.env[c] ?
                    s->algorithm.attack_coeff_per_channel[c] : s->algorithm.release_coeff_per_channel[c];
                s->algorithm.env[c]+=coefficient*(magnitude-s->algorithm.env[c]);
                float gain=1;
                if(s->algorithm.env[c]>s->algorithm.threshold_linear && s->algorithm.env[c]>0) {
                    gain=s->algorithm.k1[c]*s->algorithm.threshold_linear/s->algorithm.env[c];
                    if(gain<s->algorithm.max_attack[c]) gain=s->algorithm.max_attack[c];
                }
                s->gain=gain;io->out.samples[index]=x*gain;
            }
        }
    }
    io->out.buffer->frame_count=frames;
    return orpheus_builtin_limiter_emit_out(ctx,&io->out);
}
