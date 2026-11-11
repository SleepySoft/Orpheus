#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse(const char* text, float* output, unsigned capacity, bool single, unsigned* parsed) {
    unsigned count=0;
    while (isspace((unsigned char)*text)) ++text;
    if (*text) for (;;) {
        char* end;
        float value=single ? strtof(text,&end) : (float)strtod(text,&end);
        if (end==text || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        if (output && count<capacity) output[count]=value;
        ++count;
        while (isspace((unsigned char)*end)) ++end;
        if (!*end) break;
        if (*end!=',') return ORPHEUS_ERR_INVALID_ARG;
        text=end+1;
    }
    *parsed=count;
    return ORPHEUS_OK;
}

int orpheus_builtin_slc_matrix_mul_validate(const orpheus_builtin_slc_matrix_mul_Parameters* p) {
    unsigned parsed=0,count=(unsigned)p->rows*p->cols;
    if (p->execution_mode==1) {
        int rc=parse(p->requested_gains,NULL,count,true,&parsed);
        return rc ? rc : parsed && parsed!=count ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
    }
    int rc=parse(p->tables,NULL,(unsigned)p->num_tables*count,false,&parsed);
    if (rc) return rc;
    float axis[8];
    rc=parse(p->interp_x,axis,(unsigned)p->num_tables,false,&parsed);
    if (rc) return rc;
    if (parsed>=(unsigned)p->num_tables)
        for (int32_t i=1;i<p->num_tables;++i)
            if (axis[i]<=axis[i-1] || !isfinite(axis[i]-axis[i-1]))
                return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}

static void compute_target(orpheus_builtin_slc_matrix_mul_State* s) {
    unsigned count=(unsigned)s->rows*s->cols,n=(unsigned)s->num_tables;
    float index=s->interp_index;
    const float* a=s->algorithm.tables;
    if (n==1 || index<=s->algorithm.interp_x[0]) {
        memcpy(s->accepted_targets,a,count*sizeof(float));
    } else if (index>=s->algorithm.interp_x[n-1]) {
        memcpy(s->accepted_targets,a+(n-1)*count,count*sizeof(float));
    } else {
        unsigned i=0;
        while (i+1<n && index>=s->algorithm.interp_x[i+1]) ++i;
        float span=s->algorithm.interp_x[i+1]-s->algorithm.interp_x[i];
        float t=span>1e-12f ? (index-s->algorithm.interp_x[i])/span : 0.0f;
        if (t<0) t=0;
        if (t>1) t=1;
        a+=i*count;
        const float* b=a+count;
        for (unsigned k=0;k<count;++k)
            s->accepted_targets[k]=s->interp_method==1
                ? expf((1-t)*logf(fmaxf(a[k],SLC_MM_SILENT_GAIN))+
                    t*logf(fmaxf(b[k],SLC_MM_SILENT_GAIN)))
                : (1-t)*a[k]+t*b[k];
    }
    s->algorithm.last_index=index;
}

int orpheus_builtin_slc_matrix_mul_prepare(orpheus_builtin_slc_matrix_mul_State* s,
    const OrpheusConfig* config) {
    (void)config;
    memset(&s->algorithm,0,sizeof(s->algorithm));
    s->last_sequence=0;s->initialized=false;
    unsigned count=(unsigned)s->rows*s->cols,parsed=0;
    if (s->execution_mode==1)
        return parse(s->requested_gains,s->algorithm.requested_gains,count,true,&parsed);
    int rc=parse(s->tables,s->algorithm.tables,(unsigned)s->num_tables*count,false,&parsed);
    if (rc) return rc;
    if (parsed<count)
        for (int32_t i=0;i<s->rows && i<s->cols;++i) s->algorithm.tables[i*s->cols+i]=1;
    rc=parse(s->interp_x,s->algorithm.interp_x,(unsigned)s->num_tables,false,&parsed);
    if (rc) return rc;
    if (parsed<(unsigned)s->num_tables)
        for (int32_t i=0;i<s->num_tables;++i)
            s->algorithm.interp_x[i]=s->num_tables>1 ? (float)i/(float)(s->num_tables-1) : 0;
    compute_target(s);
    memcpy(s->active_gains,s->accepted_targets,count*sizeof(float));
    return ORPHEUS_OK;
}

int orpheus_builtin_slc_matrix_mul_reset(orpheus_builtin_slc_matrix_mul_State* s) {
    if (s->execution_mode==1) {
        s->initialized=false;s->last_sequence=0;
    } else {
        compute_target(s);
        memcpy(s->active_gains,s->accepted_targets,(size_t)s->rows*s->cols*sizeof(float));
    }
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_slc_matrix_mul_algorithm(orpheus_builtin_slc_matrix_mul_State* s,
    orpheus_builtin_slc_matrix_mul_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count ||
        !isfinite(s->interp_index) || !isfinite(s->ramp_coeff) || s->ramp_coeff<0 || s->ramp_coeff>1)
        return ORPHEUS_ERR_INVALID_ARG;
    unsigned count=(unsigned)s->rows*s->cols;
    size_t input_bytes=(size_t)ctx->frame_count*s->cols*sizeof(float);
    size_t output_bytes=(size_t)ctx->frame_count*s->rows*sizeof(float);
    if (overlap(io->in.samples,input_bytes,io->out.samples,output_bytes) ||
        overlap(io->in.samples,input_bytes,s,sizeof(*s)) ||
        overlap(io->out.samples,output_bytes,s,sizeof(*s)))
        return ORPHEUS_ERR_INVALID_ARG;
    if (s->execution_mode==1) {
        for (unsigned k=0;k<count;++k)
            if (!isfinite(s->algorithm.requested_gains[k]) ||
                (s->initialized && (!isfinite(s->active_gains[k]) || !isfinite(s->accepted_targets[k]))))
                return ORPHEUS_ERR_INVALID_ARG;
        if (!s->initialized) {
            memcpy(s->active_gains,s->algorithm.requested_gains,count*sizeof(float));
            memcpy(s->accepted_targets,s->algorithm.requested_gains,count*sizeof(float));
            s->algorithm.active_coefficient=s->ramp_coeff;s->initialized=true;
        }
        if (s->last_sequence!=s->sequence) {
            if (!((uint32_t)s->sequence&1u)) for (unsigned k=0;k<count;++k) {
                s->active_gains[k]=fmaxf(s->active_gains[k],SLC_MM_SILENT_GAIN);
                s->accepted_targets[k]=s->algorithm.requested_gains[k];
            }
            s->last_sequence=s->sequence;
        }
    } else if (s->algorithm.last_index!=s->interp_index) {
        compute_target(s);
    }
    float c=s->execution_mode==1 ? s->algorithm.active_coefficient : s->ramp_coeff,other=1-c;
    bool frozen=s->execution_mode==1 ? ((uint32_t)s->sequence&1u)!=0 : s->freeze!=0;
    for (uint32_t n=0;n<ctx->frame_count;++n) {
        if (!frozen) for (unsigned k=0;k<count;++k) {
            if (s->execution_mode==1) s->active_gains[k]=other*s->accepted_targets[k]+c*s->active_gains[k];
            else s->active_gains[k]=c<=1e-6f ? s->accepted_targets[k] :
                c*s->active_gains[k]+other*s->accepted_targets[k];
        }
        for (int32_t r=0;r<s->rows;++r) {
            float value=0;
            for (int32_t col=0;col<s->cols;++col)
                value+=s->active_gains[r*s->cols+col]*io->in.samples[(size_t)n*s->cols+col];
            io->out.samples[(size_t)n*s->rows+r]=value;
        }
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_slc_matrix_mul_emit_out(ctx,&io->out);
}
