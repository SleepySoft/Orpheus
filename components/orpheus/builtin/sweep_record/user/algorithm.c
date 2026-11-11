#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_sweep_record_reset(orpheus_builtin_sweep_record_State* s) {
    s->algorithm.t=0;s->algorithm.total_frames=0;s->algorithm.quiet_frames=0;
    s->algorithm.peak_rms=0;s->algorithm.done=false;s->progress=0;s->current_freq=0;
    memset(s->algorithm.count,0,sizeof(s->algorithm.count));
    memset(s->algorithm.acc,0,sizeof(s->algorithm.acc));
    memset(s->sweep+256,0,256*sizeof(float));
    return ORPHEUS_OK;
}

int orpheus_builtin_sweep_record_prepare(orpheus_builtin_sweep_record_State* s,const OrpheusConfig* config) {
    if(!isfinite(config->sample_rate) || config->sample_rate<=0 || config->sample_rate>UINT32_MAX)
        return ORPHEUS_ERR_INVALID_ARG;
    s->algorithm.sample_rate=config->sample_rate;
    s->algorithm.duration_frames=s->duration_s>0 ? (uint64_t)((double)s->duration_s*config->sample_rate) : UINT64_MAX;
    if(!s->algorithm.duration_frames) s->algorithm.duration_frames=1;
    memset(s->sweep,0,sizeof(s->sweep));
    for(int32_t i=0;i<s->bins;i++) {
        double u=(double)i/(s->bins-1);
        s->sweep[i]=s->log_scale ? (float)((double)s->start_freq*pow((double)s->end_freq/s->start_freq,u))
            : s->start_freq+(s->end_freq-s->start_freq)*(float)u;
    }
    return orpheus_builtin_sweep_record_reset(s);
}

static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_sweep_record_algorithm(orpheus_builtin_sweep_record_State* s,
    orpheus_builtin_sweep_record_IO* io,const OrpheusProcessContext* ctx) {
    if(!ctx->frame_count) return ORPHEUS_OK;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if(!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count ||
        ctx->sample_rate!=s->algorithm.sample_rate || !ctx->sample_rate ||
        overlap(io->in.samples,bytes,s,sizeof(*s)) || overlap(io->out.samples,bytes,s,sizeof(*s)) ||
        (io->in.samples!=io->out.samples && overlap(io->in.samples,bytes,io->out.samples,bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    if(!s->algorithm.done) {
        if(!isfinite(s->algorithm.t) || s->algorithm.t<0) return ORPHEUS_ERR_INVALID_ARG;
        if(s->algorithm.total_frames>UINT64_MAX-ctx->frame_count ||
            s->algorithm.quiet_frames>s->algorithm.total_frames) return ORPHEUS_ERR_PROCESSING;
        double duration=s->duration_s,time=s->algorithm.t;
        if(time>=duration) time=duration;
        double position=time/(duration>0 ? duration : 1);
        double f0=s->start_freq,f1=s->end_freq;
        double frequency=s->log_scale ? f0*pow(f1/f0,position) : f0+(f1-f0)*position;
        double u=f0==f1 ? 0 : s->log_scale ? log(frequency/f0)/log(f1/f0) : (frequency-f0)/(f1-f0);
        if(u<0) u=0;
        if(u>1) u=1;
        uint32_t bin=(uint32_t)(u*(s->bins-1)+.5);
        if(s->algorithm.count[bin]>s->algorithm.total_frames ||
            s->algorithm.count[bin]>UINT64_MAX-ctx->frame_count) return ORPHEUS_ERR_PROCESSING;
        double sum=0;
        for(uint32_t n=0;n<ctx->frame_count;n++) {
            double sample=io->in.samples[(size_t)n*s->channels];
            sum+=sample*sample;
        }
        s->algorithm.acc[bin]+=(float)sum;
        s->algorithm.count[bin]+=ctx->frame_count;
        s->current_freq=(float)frequency;
        s->sweep[256+bin]=sqrtf(s->algorithm.acc[bin]/(float)s->algorithm.count[bin]);
        float rms=sqrtf((float)(sum/(double)ctx->frame_count));
        if(rms>s->algorithm.peak_rms) s->algorithm.peak_rms=rms;
        if(s->algorithm.peak_rms>0 && rms<s->algorithm.peak_rms*.02f)
            s->algorithm.quiet_frames+=ctx->frame_count;
        else s->algorithm.quiet_frames=0;
        s->algorithm.t+=(double)ctx->frame_count/ctx->sample_rate;
        s->algorithm.total_frames+=ctx->frame_count;
        uint64_t quiet_target=(uint64_t)ceil(s->algorithm.sample_rate*.25);
        if(s->algorithm.total_frames>=s->algorithm.duration_frames || s->algorithm.quiet_frames>=quiet_target) {
            for(int32_t i=0;i<s->bins;i++)
                s->sweep[256+i]=s->algorithm.count[i] ? sqrtf(s->algorithm.acc[i]/(float)s->algorithm.count[i]) : 0;
            s->algorithm.done=true;s->progress=1;
        } else {
            uint32_t measured=0;
            for(int32_t i=0;i<s->bins;i++) if(s->algorithm.count[i]) measured++;
            s->progress=(float)measured/(float)s->bins;
        }
    }
    if(io->in.samples!=io->out.samples) memcpy(io->out.samples,io->in.samples,bytes);
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_sweep_record_emit_out(ctx,&io->out);
}
