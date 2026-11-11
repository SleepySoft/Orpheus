#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_n_way_mux_normalize(orpheus_builtin_n_way_mux_Parameters* p, const char* key) {
    int32_t selected=p->select;
    if ((!key && p->select_index>=0) || (key && !strcmp(key,"select_index"))) {
        selected=p->select_index<0 ? 1 :
            p->select_index>=p->inputs ? p->inputs : p->select_index+1;
    }
    if (selected<1) selected=1;
    if (selected>p->inputs) selected=p->inputs;
    p->select=selected;
    p->select_index=selected-1;
    return ORPHEUS_OK;
}

int orpheus_builtin_n_way_mux_reset(orpheus_builtin_n_way_mux_State* s) {
    s->select_pos=(float)(s->select-1);
    return ORPHEUS_OK;
}

int orpheus_builtin_n_way_mux_prepare(orpheus_builtin_n_way_mux_State* s, const OrpheusConfig* config) {
    if (!isfinite(config->sample_rate) || config->sample_rate<0) return ORPHEUS_ERR_INVALID_ARG;
    if (s->ramp_ms>0 && config->sample_rate>0) {
        float samples=s->ramp_ms/1000.0f*(float)config->sample_rate;
        s->algorithm.ramp_step=1.0f/samples;
    } else s->algorithm.ramp_step=32.0f;
    return orpheus_builtin_n_way_mux_reset(s);
}

static bool overlap(const void* a,size_t na,const void* b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na && nb && (x>=y ? x-y<nb : y-x<na);
}

int orpheus_builtin_n_way_mux_algorithm(orpheus_builtin_n_way_mux_State* s,
    orpheus_builtin_n_way_mux_IO* io,const OrpheusProcessContext* ctx) {
    if (!io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames=ctx->frame_count,channels=(uint32_t)s->channels;
    size_t bytes=(size_t)frames*channels*sizeof(float);
    if (overlap(io->out.samples,bytes,s,sizeof(*s))) return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t i=0;i<io->in_count;i++) {
        if (io->in[i].buffer && (io->in[i].buffer->frame_count<frames ||
            overlap(io->in[i].samples,bytes,io->out.samples,bytes) ||
            overlap(io->in[i].samples,bytes,s,sizeof(*s)))) return ORPHEUS_ERR_INVALID_ARG;
    }
    float target=(float)(s->select-1),pos=s->select_pos;
    if (s->algorithm.ramp_step>=32.0f) pos=target;
    if (pos==target) {
        s->select_pos=pos;
        uint32_t index=(uint32_t)(pos+.5f);
        if (bytes) {
            if (io->in[index].buffer) memcpy(io->out.samples,io->in[index].samples,bytes);
            else memset(io->out.samples,0,bytes);
        }
        return orpheus_builtin_n_way_mux_emit_out(ctx,&io->out);
    }
    for (uint32_t n=0;n<frames;n++) {
        if (pos<target) { pos+=s->algorithm.ramp_step; if (pos>target) pos=target; }
        else if (pos>target) { pos-=s->algorithm.ramp_step; if (pos<target) pos=target; }
        uint32_t a=(uint32_t)pos,b=a+1;
        float f=pos-(float)a;
        if (b>=(uint32_t)s->inputs) { b=a; f=0; }
        const float* pa=io->in[a].buffer ? io->in[a].samples+(size_t)n*channels : NULL;
        const float* pb=io->in[b].buffer ? io->in[b].samples+(size_t)n*channels : NULL;
        float wa=1.0f-f,wb=f;
        float* output=io->out.samples+(size_t)n*channels;
        if (pa && pb) for (uint32_t c=0;c<channels;c++) output[c]=pa[c]*wa+pb[c]*wb;
        else if (pa) for (uint32_t c=0;c<channels;c++) output[c]=pa[c]*wa;
        else if (pb) for (uint32_t c=0;c<channels;c++) output[c]=pb[c]*wb;
        else for (uint32_t c=0;c<channels;c++) output[c]=0.0f;
    }
    s->select_pos=pos;
    return orpheus_builtin_n_way_mux_emit_out(ctx,&io->out);
}
