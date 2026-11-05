#include "orpheus_scalar_memory.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {.id="initial", .name="首次反馈读值", .type=ORPHEUS_VALUE_FLOAT,
     .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .readback=true},
    {.id="write_after_call", .name="续调返回后提交反馈", .type=ORPHEUS_VALUE_BOOL,
     .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=false},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED}
};
#define PORT(ID, DIR, FORMAT) {.id=ID, .direction=DIR, .type=ORPHEUS_PORT_BULK, \
    .sample_format=FORMAT, .channels=1, .block_size=1}
static const OrpheusPort ports[] = {
    PORT("in", ORPHEUS_PORT_INPUT, ORPHEUS_FORMAT_F32),
    PORT("update", ORPHEUS_PORT_INPUT, ORPHEUS_FORMAT_I32),
    PORT("initial", ORPHEUS_PORT_INPUT, ORPHEUS_FORMAT_F32),
    PORT("out", ORPHEUS_PORT_OUTPUT, ORPHEUS_FORMAT_F32),
    PORT("enabled", ORPHEUS_PORT_OUTPUT, ORPHEUS_FORMAT_I32)
};
#undef PORT
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.scalar_memory", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports, .port_count=5, .params=params, .param_count=2,
    .state_size=sizeof(ScalarMemoryState), .alignment=8, .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    bool external=config && config->state_block;
    *state=external ? config->state_block : calloc(1, sizeof(ScalarMemoryState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(*state, 0, sizeof(ScalarMemoryState));
    ((ScalarMemoryState*)*state)->owns_memory=!external;
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((ScalarMemoryState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "initial")) return ORPHEUS_ERR_UNSUPPORTED;
    if (value->type!=ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
    ((ScalarMemoryState*)state)->initial=value->value.f32;
    return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    const ScalarMemoryState* s=(const ScalarMemoryState*)state;
    if (!strcmp(key, "initial")) { value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->initial; }
    else if (!strcmp(key, "write_after_call")) { value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->write_after_call; }
    else return ORPHEUS_ERR_UNSUPPORTED;
    return ORPHEUS_OK;
}
static int prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    ScalarMemoryState next={.owns_memory=((ScalarMemoryState*)state)->owns_memory};
    for (unsigned i=0; i<config->param_count; ++i) {
        const char* key=config->param_ids[i]; const OrpheusValue* v=config->param_values+i;
        if (!key) return ORPHEUS_ERR_INVALID_ARG;
        if (!strcmp(key, "write_after_call")) {
            if (v->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
            next.write_after_call=v->value.b;
        } else {
            int r=set_parameter(&next, key, v); if (r) return r;
        }
    }
    *(ScalarMemoryState*)state=next;
    return ORPHEUS_OK;
}
static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    ScalarMemoryState* s=(ScalarMemoryState*)state; s->value=0; s->initialized=false;
    return ORPHEUS_OK;
}
static bool valid(const OrpheusBuffer* b, OrpheusSampleFormat format, bool ready) {
    return b && b->data && b->format==format && b->channels==1 && b->interleaved &&
        b->frame_capacity>=1 && (!ready || b->frame_count==1);
}
static int process(void* state, const OrpheusProcessContext* context) {
    if (!state || !context || !context->inputs || !context->outputs ||
        context->input_count<3 || context->output_count<1) return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return ORPHEUS_OK;
    ScalarMemoryState* s=(ScalarMemoryState*)state;
    const OrpheusBuffer* in=context->inputs[0];
    const OrpheusBuffer* gate=context->inputs[1];
    const OrpheusBuffer* initial=context->inputs[2];
    OrpheusBuffer* out=context->outputs[0];
    OrpheusBuffer* enabled=context->output_count>1 ? context->outputs[1] : NULL;
    if (!valid(in, ORPHEUS_FORMAT_F32, false) ||
        (gate && !valid(gate, ORPHEUS_FORMAT_I32, true)) ||
        (initial && !valid(initial, ORPHEUS_FORMAT_F32, true)) ||
        (out && !valid(out, ORPHEUS_FORMAT_F32, false)) ||
        (enabled && !valid(enabled, ORPHEUS_FORMAT_I32, false))) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned j=0; j<2; ++j) {
        OrpheusBuffer* target=j ? enabled : out; if (!target) continue;
        for (unsigned i=0; i<3; ++i) {
            const OrpheusBuffer* b=context->inputs[i]; if (!b) continue;
            uintptr_t a=(uintptr_t)b->data, z=(uintptr_t)target->data;
            if ((a>=z ? a-z : z-a)<sizeof(float)) return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    if (out && enabled) {
        uintptr_t a=(uintptr_t)out->data, z=(uintptr_t)enabled->data;
        if ((a>=z ? a-z : z-a)<sizeof(float)) return ORPHEUS_ERR_INVALID_ARG;
    }
    int32_t update=1; if (gate) memcpy(&update, gate->data, sizeof(update));
    float value=s->value;
    if (s->write_after_call && !s->initialized) {
        value=s->initial; if (initial) memcpy(&value, initial->data, sizeof(value));
    }
    if (!s->write_after_call && update) {
        if (!valid(in, ORPHEUS_FORMAT_F32, true)) return ORPHEUS_ERR_INVALID_ARG;
        memcpy(&value, in->data, sizeof(value)); s->value=value; s->initialized=true;
    }
    if (out) { memcpy(out->data, &value, sizeof(value)); out->frame_count=1; }
    if (enabled) { memcpy(enabled->data, &update, sizeof(update)); enabled->frame_count=1; }
    int r=orpheus_process_next(context);
    if (r || !s->write_after_call || !update) return r;
    if (!valid(in, ORPHEUS_FORMAT_F32, true)) return ORPHEUS_ERR_INVALID_ARG;
    memcpy(&s->value, in->data, sizeof(s->value)); s->initialized=true;
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface interface = {
    .get_descriptor=get_descriptor, .create=create, .destroy=destroy, .prepare=prepare, .reset=reset,
    .process=process, .set_parameter=set_parameter, .get_parameter=get_parameter
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
