#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_piecewise_soft_clipper_validate(const orpheus_builtin_piecewise_soft_clipper_Parameters* p) {
    return p->xmin>p->xmax || p->xmin_low>p->xmax_low ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

int orpheus_builtin_piecewise_soft_clipper_reset(orpheus_builtin_piecewise_soft_clipper_State* s) {
    s->active_mask=0;
    return ORPHEUS_OK;
}

int orpheus_builtin_piecewise_soft_clipper_prepare(orpheus_builtin_piecewise_soft_clipper_State* s,
    const OrpheusConfig* config) {
    (void)config;
    return orpheus_builtin_piecewise_soft_clipper_reset(s);
}

int orpheus_builtin_piecewise_soft_clipper_algorithm(orpheus_builtin_piecewise_soft_clipper_State* s,
    orpheus_builtin_piecewise_soft_clipper_IO* io,const OrpheusProcessContext* ctx) {
    if(!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    uintptr_t x=(uintptr_t)io->in.samples,y=(uintptr_t)io->out.samples,state=(uintptr_t)s;
    if((x!=y && (x>=y ? x-y<bytes : y-x<bytes)) ||
        (bytes && (y>=state ? y-state<sizeof(*s) : state-y<bytes)) ||
        (bytes && (x>=state ? x-state<sizeof(*s) : state-x<bytes))) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t mask=0;
    if(s->disabled) {
        if(bytes && x!=y) memcpy(io->out.samples,io->in.samples,bytes);
    } else {
        float xmin=s->param_set ? s->xmin : s->xmin_low;
        float xmax=s->param_set ? s->xmax : s->xmax_low;
        float p2=s->param_set ? s->p2 : s->p2_low;
        for(int32_t c=0;c<s->channels;c++) {
            float peak=ctx->frame_count ? fabsf(io->in.samples[c]) : 0.0f;
            for(uint32_t n=0;n<ctx->frame_count;n++) {
                size_t i=(size_t)n*s->channels+c;
                float sample=io->in.samples[i],magnitude=fabsf(sample);
                float limited=fminf(magnitude,xmax),excess=fmaxf(limited-xmin,0.0f);
                io->out.samples[i]=copysignf(limited-excess*excess*p2,sample);
                if(magnitude>peak) peak=magnitude;
            }
            if(peak>xmin) mask|=UINT32_C(1)<<c;
        }
    }
    memcpy(&s->active_mask,&mask,sizeof(mask));
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_piecewise_soft_clipper_emit_out(ctx,&io->out);
}
