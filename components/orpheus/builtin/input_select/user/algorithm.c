#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int parse(const char* text, int32_t* map, int32_t inputs, int32_t outputs, bool override) {
    while (isspace((unsigned char)*text)) ++text;
    if (!*text) return ORPHEUS_OK;
    for (int32_t c=0;c<outputs;++c) {
        char* end;
        errno=0;
        long value=strtol(text,&end,10);
        if (end==text || errno==ERANGE || value<INT32_MIN || value>INT32_MAX)
            return ORPHEUS_ERR_INVALID_ARG;
        if (!override || value)
            map[c]=value>0 && value<=inputs ? (int32_t)value-1 : -1;
        while (isspace((unsigned char)*end)) ++end;
        if (!*end) return ORPHEUS_OK;
        if (c+1==outputs || *end!=',') return ORPHEUS_ERR_INVALID_ARG;
        text=end+1;
    }
    return ORPHEUS_ERR_INVALID_ARG;
}

static int build_map(int32_t inputs, int32_t outputs, const char* base, const char* override,
    int32_t* base_map, int32_t* map) {
    for (int32_t c=0;c<32;++c) base_map[c]=c<inputs ? c : -1;
    int rc=parse(base,base_map,inputs,outputs,false);
    if (rc) return rc;
    memcpy(map,base_map,32*sizeof(int32_t));
    return parse(override,map,inputs,outputs,true);
}

int orpheus_builtin_input_select_validate(const orpheus_builtin_input_select_Parameters* p) {
    int32_t base[32],map[32];
    return build_map(p->channels_in,p->channels_out,p->select,p->override_select,base,map);
}

int orpheus_builtin_input_select_prepare(orpheus_builtin_input_select_State* s, const OrpheusConfig* config) {
    (void)config;
    return build_map(s->channels_in,s->channels_out,s->select,s->override_select,
        s->algorithm.base_map,s->algorithm.map);
}

int orpheus_builtin_input_select_reset(orpheus_builtin_input_select_State* s) {
    (void)s;
    return ORPHEUS_OK;
}

int orpheus_builtin_input_select_set_parameter(orpheus_builtin_input_select_State* s,
    const char* key, const OrpheusValue* value) {
    if (strcmp(key,"override_select")) return ORPHEUS_ERR_UNSUPPORTED;
    int32_t map[32];
    memcpy(map,s->algorithm.base_map,sizeof(map));
    int rc=parse(value->value.str,map,s->channels_in,s->channels_out,true);
    if (!rc) memcpy(s->algorithm.map,map,sizeof(map));
    return rc;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<nb : y-x<na;
}

int orpheus_builtin_input_select_algorithm(orpheus_builtin_input_select_State* s,
    orpheus_builtin_input_select_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count<ctx->frame_count ||
        overlap(io->in.samples,(size_t)ctx->frame_count*s->channels_in*sizeof(float),
            io->out.samples,(size_t)ctx->frame_count*s->channels_out*sizeof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t n=0;n<ctx->frame_count;++n) {
        for (int32_t c=0;c<s->channels_out;++c) {
            int32_t index=s->algorithm.map[c];
            float* output=io->out.samples+(size_t)n*s->channels_out+c;
            if (index>=0) memcpy(output,io->in.samples+(size_t)n*s->channels_in+index,sizeof(float));
            else *output=0.0f;
        }
    }
    io->out.buffer->frame_count=ctx->frame_count;
    return orpheus_builtin_input_select_emit_out(ctx,&io->out);
}
