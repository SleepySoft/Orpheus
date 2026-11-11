#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int configure(const char* text, int32_t inputs, int32_t outputs, float* matrix) {
    if (matrix) {
        memset(matrix,0,1024*sizeof(float));
        for (int32_t c=0;c<inputs && c<outputs;++c) matrix[c*32+c]=1.0f;
    }
    while (isspace((unsigned char)*text)) ++text;
    if (!strncmp(text,"identity",8)) {
        const char* end=text+8;
        while (isspace((unsigned char)*end)) ++end;
        return *end ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
    }
    for (int32_t i=0;*text;++i) {
        if (i==inputs*outputs) return ORPHEUS_ERR_INVALID_ARG;
        char* end;
        errno=0;
        float value=strtof(text,&end);
        if (end==text || errno==ERANGE || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        if (matrix) matrix[(i/inputs)*32+i%inputs]=value;
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

int orpheus_builtin_output_router_validate(const orpheus_builtin_output_router_Parameters* p) {
    return configure(p->matrix,p->channels_in,p->channels_out,NULL);
}

int orpheus_builtin_output_router_prepare(orpheus_builtin_output_router_State* s, const OrpheusConfig* config) {
    (void)config;
    return configure(s->matrix,s->channels_in,s->channels_out,s->algorithm.matrix);
}

int orpheus_builtin_output_router_reset(orpheus_builtin_output_router_State* s) {
    (void)s;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_output_router_algorithm(orpheus_builtin_output_router_State* s,
    orpheus_builtin_output_router_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    size_t input_bytes=(size_t)ctx->frame_count*s->channels_in*sizeof(float);
    size_t output_bytes=(size_t)ctx->frame_count*s->channels_out*sizeof(float);
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count ||
        overlap(io->in.samples,input_bytes,io->out.samples,output_bytes) ||
        overlap(io->out.samples,output_bytes,s->algorithm.matrix,sizeof(s->algorithm.matrix)))
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t n=0;n<ctx->frame_count;++n) {
        for (int32_t r=0;r<s->channels_out;++r) {
            float acc=0.0f;
            for (int32_t c=0;c<s->channels_in;++c) {
                float gain=s->algorithm.matrix[r*32+c];
                if (gain!=0.0f) acc+=gain*io->in.samples[(size_t)n*s->channels_in+c];
            }
            io->out.samples[(size_t)n*s->channels_out+r]=acc;
        }
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_output_router_emit_out(ctx,&io->out);
}
