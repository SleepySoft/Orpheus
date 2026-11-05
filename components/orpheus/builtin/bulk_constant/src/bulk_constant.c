#include "orpheus_bulk_constant.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {.id="elements", .name="元素数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .min_i32=1, .max_i32=2048,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .affects_signature=true},
    {.id="values", .name="常量值列表", .type=ORPHEUS_VALUE_STRING,
     .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="0,1"},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED}
};
static const OrpheusPort ports[] = {
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1},
    {.id="first", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .block_size=1},
    {.id="last", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .block_size=1}
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.bulk_constant", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports, .port_count=3, .params=params, .param_count=2, .state_size=sizeof(BulkConstantState),
    .alignment=8, .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    bool external=config && config->state_block;
    *state=external ? config->state_block : calloc(1, sizeof(BulkConstantState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state, 0, sizeof(BulkConstantState));
    ((BulkConstantState*)*state)->owns_memory=!external;
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((BulkConstantState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    BulkConstantState next={.elements=2, .owns_memory=((BulkConstantState*)state)->owns_memory};
    const char* text="0,1";
    for (unsigned i=0; i<config->param_count; ++i) {
        const char* key=config->param_ids[i]; const OrpheusValue* value=config->param_values+i;
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        if (!strcmp(key, "elements")) {
            if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<1 || value->value.i32>2048)
                return ORPHEUS_ERR_INVALID_ARG;
            next.elements=(uint32_t)value->value.i32;
        } else if (!strcmp(key, "values")) {
            if (value->type!=ORPHEUS_VALUE_STRING || !value->value.str) return ORPHEUS_ERR_INVALID_ARG;
            text=value->value.str;
        } else return ORPHEUS_ERR_INVALID_ARG;
    }
    for (unsigned i=0; i<next.elements; ++i) {
        while (isspace((unsigned char)*text)) ++text;
        char* end; errno=0;
        float value=strtof(text, &end);
        if (end==text || !isfinite(value) || (errno==ERANGE && value==0.0f))
            return ORPHEUS_ERR_INVALID_ARG;
        next.values[i]=value; text=end;
        while (isspace((unsigned char)*text)) ++text;
        if (i+1<next.elements) {
            if (*text++!=',') return ORPHEUS_ERR_INVALID_ARG;
        } else if (*text) return ORPHEUS_ERR_INVALID_ARG;
    }
    *(BulkConstantState*)state=next;
    return ORPHEUS_OK;
}
static int reset(void* state) { return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG; }
static bool valid(const OrpheusBuffer* b, unsigned elements) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->channels==1 && b->frame_capacity>=elements;
}
static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b; return x>=y ? x-y<nb : y-x<na;
}
static int process(void* state, const OrpheusProcessContext* context) {
    if (!state || !context || !context->outputs || context->output_count<3)
        return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return ORPHEUS_OK;
    const BulkConstantState* s=(const BulkConstantState*)state;
    if (!s->elements || s->elements>2048) return ORPHEUS_ERR_INVALID_ARG;
    unsigned counts[]={s->elements, 1, 1};
    for (unsigned i=0; i<3; ++i) {
        OrpheusBuffer* out=context->outputs[i];
        if (!out) continue;
        if (!valid(out, counts[i]) || overlap(out->data, counts[i]*sizeof(float), s, sizeof(*s)))
            return ORPHEUS_ERR_INVALID_ARG;
        for (unsigned j=0; j<i; ++j) {
            const OrpheusBuffer* previous=context->outputs[j];
            if (previous && overlap(out->data, counts[i]*sizeof(float), previous->data, counts[j]*sizeof(float)))
                return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    for (unsigned i=0; i<3; ++i) {
        OrpheusBuffer* out=context->outputs[i];
        if (out) {
            memcpy(out->data, s->values+(i==2 ? s->elements-1 : 0), counts[i]*sizeof(float));
            out->frame_count=counts[i];
        }
    }
    return orpheus_process_next(context);
}
static const OrpheusComponentInterface interface = {
    .get_descriptor=get_descriptor, .create=create, .destroy=destroy, .prepare=prepare,
    .reset=reset, .process=process
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
