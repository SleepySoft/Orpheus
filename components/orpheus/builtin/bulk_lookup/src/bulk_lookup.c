#include "orpheus_bulk_lookup.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {.id="points", .name="表点数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .min_i32=2, .max_i32=2048,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .affects_signature=true}
};
static const OrpheusPort ports[] = {
    {.id="x", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .block_size=1},
    {.id="axis", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1},
    {.id="values", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .block_size=1}
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.bulk_lookup", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports, .port_count=4, .params=params, .param_count=1, .state_size=sizeof(BulkLookupState),
    .alignment=8, .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    bool external=config && config->state_block;
    *state=external ? config->state_block : calloc(1, sizeof(BulkLookupState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state, 0, sizeof(BulkLookupState));
    ((BulkLookupState*)*state)->owns_memory=!external;
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((BulkLookupState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    uint32_t points=2;
    for (unsigned i=0; i<config->param_count; ++i) {
        const char* key=config->param_ids[i]; const OrpheusValue* value=config->param_values+i;
        if (!key || strcmp(key, "points") || value->type!=ORPHEUS_VALUE_INT ||
            value->value.i32<2 || value->value.i32>2048) return ORPHEUS_ERR_INVALID_ARG;
        points=(uint32_t)value->value.i32;
    }
    ((BulkLookupState*)state)->points=points;
    return ORPHEUS_OK;
}
static int reset(void* state) { return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG; }
static bool valid(const OrpheusBuffer* b, unsigned count, bool input) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->channels==1 && b->frame_capacity>=count && (!input || b->frame_count==count);
}
static bool overlap(const void* out, const void* input, size_t bytes) {
    uintptr_t a=(uintptr_t)out, b=(uintptr_t)input;
    return a>=b ? a-b<bytes : b-a<sizeof(float);
}
static float read_value(const OrpheusBuffer* b, unsigned i) {
    float value; memcpy(&value, (const unsigned char*)b->data+i*sizeof(float), sizeof(value));
    return value;
}
static int process(void* state, const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        context->input_count<3 || context->output_count<1) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return ORPHEUS_OK;
    const BulkLookupState* s=(const BulkLookupState*)state;
    if (s->points<2 || s->points>2048) return ORPHEUS_ERR_INVALID_ARG;
    unsigned counts[]={1, s->points, s->points};
    OrpheusBuffer* out=context->outputs[0];
    if (out && !valid(out, 1, false)) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned i=0; i<3; ++i) {
        const OrpheusBuffer* in=context->inputs[i];
        if (!valid(in, counts[i], true) ||
            (out && overlap(out->data, in->data, counts[i]*sizeof(float))))
            return ORPHEUS_ERR_INVALID_ARG;
    }
    const OrpheusBuffer* axis=context->inputs[1];
    const OrpheusBuffer* values=context->inputs[2];
    float previous=read_value(axis, 0);
    bool descending=read_value(axis, 1)<previous;
    if (!isfinite(previous)) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned i=1; i<s->points; ++i) {
        float current=read_value(axis, i);
        if (!isfinite(current) || (descending ? current>=previous : current<=previous))
            return ORPHEUS_ERR_INVALID_ARG;
        previous=current;
    }
    float x=read_value(context->inputs[0], 0), result=0.0f;
    unsigned first=descending ? s->points-1 : 0, last=descending ? 0 : s->points-1;
    if (x>=read_value(axis, first) && x<=read_value(axis, last)) {
        unsigned low=0, high=s->points-1;
        while (high-low>1) {
            unsigned mid=(low+high)/2, at=descending ? s->points-1-mid : mid;
            if (x>=read_value(axis, at)) low=mid;
            else high=mid;
        }
        unsigned left=descending ? s->points-1-low : low;
        unsigned right=descending ? s->points-1-high : high;
        float a=read_value(axis, left), t=(x-a)/(read_value(axis, right)-a);
        float y0=read_value(values, left), y1=read_value(values, right);
        if (t==0.0f) result=y0;
        else if (t==1.0f) result=y1;
        else if (y0==y1) result=y0;
        else result=(1.0f-t)*y0+t*y1;
    }
    if (out) { memcpy(out->data, &result, sizeof(result)); out->frame_count=1; }
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
