#include "component_state.h"
#include "component_api.h"
#include "orpheus_gain_array.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse_map(const char* cursor,int32_t* map,int32_t channels,int32_t rampers,int32_t mode) {
    if(mode) return orpheus_parse_gain_array(cursor,map,(unsigned)channels,true,mode==2 ? -2 : -1,rampers-1);
    memset(map,0,(size_t)channels*sizeof(*map));
    if(!*cursor) return ORPHEUS_OK;
    for(int32_t c=0;c<channels;c++) {
        char* end;
        errno=0;
        long long value=strtoll(cursor,&end,10);
        if(end==cursor || errno==ERANGE || value<INT32_MIN || value>INT32_MAX)
            return ORPHEUS_ERR_INVALID_ARG;
        map[c]=value<0 ? -1 : value>=rampers ? 0 : (int32_t)value;
        while(isspace((unsigned char)*end)) end++;
        if(!*end) return ORPHEUS_OK;
        if(*end!=',' || c+1==channels || !end[1]) return ORPHEUS_ERR_INVALID_ARG;
        cursor=end+1;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_gain_ramper_validate(const orpheus_builtin_gain_ramper_Parameters* p) {
    int32_t integers[GR_MAX_CHANNELS];
    float values[GR_MAX_RAMPERS];
    if(p->gain_output && p->gain_channel>=p->channels) return ORPHEUS_ERR_INVALID_ARG;
    int rc=parse_map(p->chan_map,integers,p->channels,p->num_rampers,p->execution_mode);
    if(rc || !p->execution_mode) return rc;
    if(orpheus_parse_gain_array(p->current_gains,values,p->num_rampers,false,0,0) ||
       orpheus_parse_gain_array(p->target_gains,values,p->num_rampers,false,0,0) ||
       orpheus_parse_gain_array(p->coefficients,values,p->num_rampers,false,0,0) ||
       orpheus_parse_gain_array(p->remaining_blocks,integers,p->num_rampers,true,
           p->execution_mode==2 ? INT32_MIN : 0,INT32_MAX)) return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}

int orpheus_builtin_gain_ramper_reset(orpheus_builtin_gain_ramper_State* s) {
    if(s->execution_mode) return ORPHEUS_OK;
    float target=powf(10.0f,s->gain_db/20.0f);
    for(int32_t r=0;r<s->num_rampers;r++)
        s->algorithm.rampers[r]=(RamperSlot){target,target,0};
    return ORPHEUS_OK;
}

int orpheus_builtin_gain_ramper_prepare(orpheus_builtin_gain_ramper_State* s,const OrpheusConfig* config) {
    s->algorithm.sampleRate=config->sample_rate ? (float)config->sample_rate : 48000.0f;
    s->algorithm.blockSize=config->block_size ? config->block_size : 32;
    int rc=parse_map(s->chan_map,s->algorithm.chanMap,s->channels,s->num_rampers,s->execution_mode);
    if(rc) return rc;
    if(!s->execution_mode) return orpheus_builtin_gain_ramper_reset(s);
    if(orpheus_parse_gain_array(s->current_gains,s->algorithm.current_gains,s->num_rampers,false,0,0) ||
       orpheus_parse_gain_array(s->target_gains,s->algorithm.target_gains,s->num_rampers,false,0,0) ||
       orpheus_parse_gain_array(s->coefficients,s->algorithm.coefficients,s->num_rampers,false,0,0) ||
       orpheus_parse_gain_array(s->remaining_blocks,s->algorithm.remaining_blocks,s->num_rampers,true,
           s->execution_mode==2 ? INT32_MIN : 0,INT32_MAX)) return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}

int orpheus_builtin_gain_ramper_set_parameter(orpheus_builtin_gain_ramper_State* s,
    const char* key,const OrpheusValue* value) {
    if(!strcmp(key,"freeze")) return s->execution_mode ? ORPHEUS_OK : ORPHEUS_ERR_UNSUPPORTED;
    if(strcmp(key,"gain_db")) return ORPHEUS_ERR_NOT_FOUND;
    if(s->execution_mode) return ORPHEUS_ERR_UNSUPPORTED;
    float target=fmaxf(powf(10.0f,value->value.f32/20.0f),GR_SILENT_GAIN);
    for(int32_t i=0;i<s->num_rampers;i++) {
        RamperSlot* r=&s->algorithm.rampers[i];
        r->targetGain=target;
        float current_db=20.0f*log10f(fmaxf(r->currentGain,GR_SILENT_GAIN));
        float target_db=20.0f*log10f(target);
        float difference=fabsf(target_db-current_db);
        if(difference<0.01f || s->ramp_ms<=0 || s->algorithm.sampleRate<=0 || !s->algorithm.blockSize) {
            r->rampCoeff=0;
            r->currentGain=target;
        } else {
            float blocks=s->ramp_ms/1000.0f*s->algorithm.sampleRate/(float)s->algorithm.blockSize;
            if(blocks<1) blocks=1;
            r->rampCoeff=logf(target/fmaxf(r->currentGain,GR_SILENT_GAIN))/blocks;
        }
    }
    return ORPHEUS_OK;
}

static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}

int orpheus_builtin_gain_ramper_algorithm(orpheus_builtin_gain_ramper_State* s,
    orpheus_builtin_gain_ramper_IO* io,const OrpheusProcessContext* ctx) {
    if(!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if(overlap(io->in.samples,bytes,s,sizeof(*s)) || overlap(io->out.samples,bytes,s,sizeof(*s)) ||
       (io->in.samples!=io->out.samples && overlap(io->in.samples,bytes,io->out.samples,bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    if(io->gain_count && io->gain[0].buffer &&
       (overlap(io->gain[0].samples,sizeof(float),s,sizeof(*s)) ||
        overlap(io->gain[0].samples,sizeof(float),io->in.samples,bytes) ||
        overlap(io->gain[0].samples,sizeof(float),io->out.samples,bytes))) return ORPHEUS_ERR_INVALID_ARG;
    if(s->execution_mode) {
        for(int32_t r=0;r<s->num_rampers;r++)
            if(!isfinite(s->algorithm.current_gains[r]) || s->algorithm.current_gains[r]<0 ||
               !isfinite(s->algorithm.target_gains[r]) || s->algorithm.target_gains[r]<0 ||
               !isfinite(s->algorithm.coefficients[r]) || s->algorithm.coefficients[r]<0 ||
               (s->execution_mode==1 && s->algorithm.remaining_blocks[r]<0)) return ORPHEUS_ERR_INVALID_ARG;
    }
    for(int32_t c=0;c<s->channels;c++)
        if(s->algorithm.chanMap[c]<(s->execution_mode==2 ? -2 : -1) ||
            s->algorithm.chanMap[c]>=s->num_rampers) return ORPHEUS_ERR_INVALID_ARG;
    if(s->execution_mode && !ctx->frame_count) return ORPHEUS_OK;
    if(s->execution_mode) {
        float gains[GR_MAX_RAMPERS],starts[GR_MAX_RAMPERS],coefficients[GR_MAX_RAMPERS];
        for(int32_t r=0;r<s->num_rampers;r++) {
            gains[r]=s->algorithm.current_gains[r];coefficients[r]=1;
            if(!s->freeze) {
                if(!s->algorithm.remaining_blocks[r]) gains[r]=s->algorithm.target_gains[r];
                else {
                    coefficients[r]=s->algorithm.coefficients[r];
                    if(s->algorithm.remaining_blocks[r]>INT32_MIN) s->algorithm.remaining_blocks[r]--;
                }
            }
            starts[r]=gains[r];
        }
        for(uint32_t n=0;n<ctx->frame_count;n++) {
            for(int32_t r=0;r<s->num_rampers;r++) {
                if(s->execution_mode==2)
                    gains[r]=starts[r]*(coefficients[r]==1 ? 1 : powf(coefficients[r],(float)(n+1)));
                else gains[r]*=coefficients[r];
            }
            for(int32_t c=0;c<s->channels;c++) {
                int32_t r=s->algorithm.chanMap[c];
                size_t index=(size_t)n*s->channels+c;
                if(r==-2) io->out.samples[index]=0;
                else if(r<0) {
                    if(io->out.samples!=io->in.samples) memcpy(io->out.samples+index,io->in.samples+index,sizeof(float));
                } else io->out.samples[index]=io->in.samples[index]*gains[r];
            }
        }
        memcpy(s->algorithm.current_gains,gains,(size_t)s->num_rampers*sizeof(float));
    } else {
        for(int32_t r=0;r<s->num_rampers;r++) {
            RamperSlot* rp=&s->algorithm.rampers[r];
            if(rp->rampCoeff!=0) {
                rp->currentGain*=expf(rp->rampCoeff);
                if((rp->rampCoeff>0 && rp->currentGain>=rp->targetGain) ||
                   (rp->rampCoeff<0 && rp->currentGain<=rp->targetGain)) {
                    rp->currentGain=rp->targetGain;rp->rampCoeff=0;
                }
            }
            if(rp->currentGain<GR_SILENT_GAIN) rp->currentGain=GR_SILENT_GAIN;
        }
        for(uint32_t n=0;n<ctx->frame_count;n++) for(int32_t c=0;c<s->channels;c++) {
            int32_t r=s->algorithm.chanMap[c];
            float gain=r>=0 ? s->algorithm.rampers[r].currentGain : 1;
            size_t index=(size_t)n*s->channels+c;
            io->out.samples[index]=io->in.samples[index]*gain;
        }
    }
    io->out.buffer->frame_count=ctx->frame_count;
    if(ctx->frame_count && io->gain_count && io->gain[0].buffer) {
        int32_t group=s->algorithm.chanMap[s->gain_channel];
        io->gain[0].samples[0]=group==-2 && s->execution_mode==2 ? 0 : group<0 ? 1 :
            s->execution_mode ? s->algorithm.current_gains[group] : s->algorithm.rampers[group].currentGain;
        io->gain[0].buffer->frame_count=1;
    }
    int rc=orpheus_builtin_gain_ramper_emit_out(ctx,&io->out);
    if(rc) return rc;
    return ctx->frame_count && io->gain_count && io->gain[0].buffer ?
        orpheus_builtin_gain_ramper_emit_gain(ctx,&io->gain[0]) : ORPHEUS_OK;
}
