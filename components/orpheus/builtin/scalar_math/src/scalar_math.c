#include "orpheus_scalar_math.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {.id="operation", .name="基础运算", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=5}, .min_i32=0, .max_i32=5,
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .readback=true},
    {.id="a", .name="未连接a的值", .type=ORPHEUS_VALUE_FLOAT,
     .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .readback=true},
    {.id="b", .name="未连接b的值", .type=ORPHEUS_VALUE_FLOAT,
     .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .readback=true},
    {.id="c", .name="未连接c的值", .type=ORPHEUS_VALUE_FLOAT,
     .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .readback=true}
};
#define PORT(ID, DIR) {.id=ID, .direction=DIR, .type=ORPHEUS_PORT_BULK, \
    .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .block_size=1}
static const OrpheusPort ports[] = {
    PORT("a", ORPHEUS_PORT_INPUT), PORT("b", ORPHEUS_PORT_INPUT),
    PORT("c", ORPHEUS_PORT_INPUT), PORT("out", ORPHEUS_PORT_OUTPUT)
};
#undef PORT
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.scalar_math", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports, .port_count=4, .params=params, .param_count=4,
    .state_size=sizeof(ScalarMathState), .alignment=8, .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    bool external=config && config->state_block;
    *state=external ? config->state_block : calloc(1, sizeof(ScalarMathState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state, 0, sizeof(ScalarMathState));
    ((ScalarMathState*)*state)->owns_memory=!external;
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((ScalarMathState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    ScalarMathState* s=(ScalarMathState*)state;
    if (!strcmp(key, "operation")) {
        if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<0 || value->value.i32>5)
            return ORPHEUS_ERR_INVALID_ARG;
        s->operation=value->value.i32;
    } else {
        if (strlen(key)!=1 || *key<'a' || *key>'c' || value->type!=ORPHEUS_VALUE_FLOAT ||
            !isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        s->values[*key-'a']=value->value.f32;
    }
    return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const ScalarMathState* s=(const ScalarMathState*)state;
    if (!strcmp(key, "operation")) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->operation;
    } else {
        if (strlen(key)!=1 || *key<'a' || *key>'c') return ORPHEUS_ERR_INVALID_ARG;
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->values[*key-'a'];
    }
    return ORPHEUS_OK;
}
static int prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    ScalarMathState next={.operation=5, .owns_memory=((ScalarMathState*)state)->owns_memory};
    for (unsigned i=0; i<config->param_count; ++i) {
        int error=set_parameter(&next, config->param_ids[i], config->param_values+i);
        if (error) return error;
    }
    *(ScalarMathState*)state=next;
    return ORPHEUS_OK;
}
static int reset(void* state) { return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG; }
static bool valid(const OrpheusBuffer* b, bool input) {
    return b && b->data && b->format==ORPHEUS_FORMAT_F32 && b->interleaved &&
        b->channels==1 && b->frame_capacity>=1 && (!input || b->frame_count==1);
}
static int process(void* state, const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        context->input_count<3 || context->output_count<1) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return ORPHEUS_OK;
    const ScalarMathState* s=(const ScalarMathState*)state;
    if (s->operation<0 || s->operation>5) return ORPHEUS_ERR_INVALID_ARG;
    float v[3]; memcpy(v, s->values, sizeof(v));
    OrpheusBuffer* out=context->outputs[0];
    if (out && !valid(out, false)) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned i=0; i<3; ++i) {
        const OrpheusBuffer* in=context->inputs[i];
        if (!in) continue;
        if (!valid(in, true)) return ORPHEUS_ERR_INVALID_ARG;
        if (out) {
            uintptr_t a=(uintptr_t)in->data, b=(uintptr_t)out->data;
            if ((a>=b ? a-b : b-a)<sizeof(float)) return ORPHEUS_ERR_INVALID_ARG;
        }
        memcpy(v+i, in->data, sizeof(float));
    }
    float result;
    switch (s->operation) {
        case 0: result=fminf(v[0], v[1]); break;
        case 1: result=fmaxf(v[0], v[1]); break;
        case 2: result=v[0]>v[1] ? 1.0f : 0.0f; break;
        case 3: result=v[0]!=v[1] ? 1.0f : 0.0f; break;
        case 4: result=v[0]!=0.0f ? v[1] : v[2]; break;
        default: result=v[0]; break;
    }
    if (out) { memcpy(out->data, &result, sizeof(result)); out->frame_count=1; }
    return orpheus_process_next(context);
}
static const OrpheusComponentInterface interface = {
    .get_descriptor=get_descriptor, .create=create, .destroy=destroy, .prepare=prepare,
    .reset=reset, .process=process, .set_parameter=set_parameter, .get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
