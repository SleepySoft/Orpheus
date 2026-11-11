#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse(const char* text, float* output) {
    if (output) memset(output,0,1024*sizeof(float));
    while (isspace((unsigned char)*text)) ++text;
    for (unsigned i=0;*text;++i) {
        if (i==1024) return ORPHEUS_ERR_INVALID_ARG;
        char* end;
        errno=0;
        double value=strtod(text,&end);
        if (end==text || errno==ERANGE || !isfinite(value) || fabs(value)>FLT_MAX)
            return ORPHEUS_ERR_INVALID_ARG;
        if (output) output[i]=(float)value;
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
    return ORPHEUS_OK;
}

int orpheus_builtin_matrix_mul_validate(const orpheus_builtin_matrix_mul_Parameters* p) {
    return parse(p->matrix,NULL);
}

int orpheus_builtin_matrix_mul_reset(orpheus_builtin_matrix_mul_State* s) {
    memset(s->algorithm.matrix,0,sizeof(s->algorithm.matrix));
    for (int32_t i=0;i<s->rows && i<s->cols;++i) s->algorithm.matrix[i*s->cols+i]=1.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_matrix_mul_prepare(orpheus_builtin_matrix_mul_State* s, const OrpheusConfig* config) {
    (void)config;
    const char* text=s->matrix;
    while (isspace((unsigned char)*text)) ++text;
    return *text ? parse(text,s->algorithm.matrix) : orpheus_builtin_matrix_mul_reset(s);
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_matrix_mul_algorithm(orpheus_builtin_matrix_mul_State* s,
    orpheus_builtin_matrix_mul_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    size_t input_bytes=(size_t)ctx->frame_count*s->cols*sizeof(float);
    size_t output_bytes=(size_t)ctx->frame_count*s->rows*sizeof(float);
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count ||
        overlap(io->in.samples,input_bytes,io->out.samples,output_bytes) ||
        overlap(io->out.samples,output_bytes,s->algorithm.matrix,sizeof(s->algorithm.matrix)))
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t n=0;n<ctx->frame_count;++n) {
        for (int32_t r=0;r<s->rows;++r) {
            float acc=0.0f;
            for (int32_t c=0;c<s->cols;++c)
                acc+=s->algorithm.matrix[r*s->cols+c]*io->in.samples[(size_t)n*s->cols+c];
            io->out.samples[(size_t)n*s->rows+r]=acc;
        }
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_matrix_mul_emit_out(ctx,&io->out);
}
