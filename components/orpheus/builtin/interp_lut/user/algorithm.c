#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse(const char* text, float* output, uint32_t* count) {
    *count=0;
    while (isspace((unsigned char)*text)) ++text;
    if (!*text) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned i=0;;++i) {
        if (i==256) return ORPHEUS_ERR_INVALID_ARG;
        char* end;
        errno=0;
        double value=strtod(text,&end);
        if (end==text || errno==ERANGE || !isfinite(value) || fabs(value)>FLT_MAX)
            return ORPHEUS_ERR_INVALID_ARG;
        output[i]=(float)value;
        *count=i+1;
        bool whitespace=isspace((unsigned char)*end)!=0;
        while (isspace((unsigned char)*end)) ++end;
        if (!*end) return ORPHEUS_OK;
        if (*end==',') {
            text=end+1;
            while (isspace((unsigned char)*text)) ++text;
            if (!*text) return ORPHEUS_ERR_INVALID_ARG;
        } else if (whitespace) text=end;
        else return ORPHEUS_ERR_INVALID_ARG;
    }
}

int orpheus_builtin_interp_lut_validate(const orpheus_builtin_interp_lut_Parameters* p) {
    float axis[256],table[256];uint32_t nx,ny;
    int rc=parse(p->x_axis,axis,&nx);
    if (rc) return rc;
    rc=parse(p->y_axis,table,&ny);
    if (rc) return rc;
    if (nx!=ny) return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t i=1;i<nx;++i)
        if (axis[i]<=axis[i-1] || !isfinite(axis[i]-axis[i-1]) || !isfinite(table[i]-table[i-1]))
            return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}

int orpheus_builtin_interp_lut_prepare(orpheus_builtin_interp_lut_State* s, const OrpheusConfig* config) {
    (void)config;
    uint32_t count;
    int rc=parse(s->x_axis,s->algorithm.axis,&s->algorithm.count);
    if (rc) return rc;
    rc=parse(s->y_axis,s->algorithm.table,&count);
    if (rc) return rc;
    s->y=0.0f;
    s->algorithm.hist_pos=s->algorithm.hist_count=0;
    memcpy(s->history,"[]",3);
    return ORPHEUS_OK;
}

int orpheus_builtin_interp_lut_reset(orpheus_builtin_interp_lut_State* s) {
    s->x=s->y=0.0f;
    s->algorithm.hist_pos=s->algorithm.hist_count=0;
    memcpy(s->history,"[]",3);
    return ORPHEUS_OK;
}

static float interpolate(const orpheus_builtin_interp_lut_State* s) {
    uint32_t count=s->algorithm.count;
    const float* axis=s->algorithm.axis;
    const float* table=s->algorithm.table;
    if (s->x<=axis[0]) return table[0];
    if (s->x>=axis[count-1]) return table[count-1];
    for (uint32_t i=0;i+1<count;++i) {
        if (s->x>=axis[i] && s->x<=axis[i+1]) {
            float span=axis[i+1]-axis[i];
            float t=(s->x-axis[i])/span;
            return table[i]+t*(table[i+1]-table[i]);
        }
    }
    return NAN;
}

static bool overlap(const void* a, const void* b, size_t bytes) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<bytes : y-x<bytes;
}

int orpheus_builtin_interp_lut_algorithm(orpheus_builtin_interp_lut_State* s,
    orpheus_builtin_interp_lut_IO* io, const OrpheusProcessContext* ctx) {
    size_t bytes=(size_t)ctx->frame_count*s->channels*sizeof(float);
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count ||
        (io->in.samples!=io->out.samples && overlap(io->in.samples,io->out.samples,bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    float value=interpolate(s);
    if (!isfinite(value)) return ORPHEUS_ERR_PROCESSING;
    s->y=value;
    s->algorithm.hist[s->algorithm.hist_pos]=value;
    s->algorithm.hist_pos=(s->algorithm.hist_pos+1)%64;
    if (s->algorithm.hist_count<64) ++s->algorithm.hist_count;
    size_t used=1;
    s->history[0]='[';
    for (uint32_t i=0;i<s->algorithm.hist_count;++i) {
        uint32_t index=(s->algorithm.hist_pos+64-s->algorithm.hist_count+i)%64;
        int n=snprintf(s->history+used,sizeof(s->history)-used,i ? ",%.4g" : "%.4g",s->algorithm.hist[index]);
        if (n<0 || (size_t)n>=sizeof(s->history)-used) return ORPHEUS_ERR_PROCESSING;
        used+=(size_t)n;
    }
    if (sizeof(s->history)-used<2) return ORPHEUS_ERR_PROCESSING;
    s->history[used++]=']';s->history[used]=0;
    if (bytes && io->in.samples!=io->out.samples) memcpy(io->out.samples,io->in.samples,bytes);
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_interp_lut_emit_out(ctx,&io->out);
}
