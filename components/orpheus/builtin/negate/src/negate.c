#include "orpheus_negate.h"
#include <stdlib.h>
#include <string.h>

static int neg_create(void** state, const OrpheusConfig* config) {
    NegateState* s = (NegateState*)calloc(1, sizeof(NegateState));
    if (config != NULL && config->state_block != NULL) { *state = config->state_block; memset(*state,0,sizeof(NegateState)); return ORPHEUS_OK; }
    *state = s;
    return s ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int neg_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int neg_prepare(void* state, const OrpheusConfig* config) {
    NegateState* s = (NegateState*)state;
    s->channels = config->channels > 0 ? (config->channels < NEG_MAX_CH ? config->channels : NEG_MAX_CH) : 2;
    return ORPHEUS_OK;
}
static int neg_reset(void* state) { (void)state; return ORPHEUS_OK; }

/* 变号：out = -in （反相） */
static int neg_process(void* state, const OrpheusProcessContext* ctx) {
    NegateState* s = (NegateState*)state;
    if (ctx->input_count < 1 || !ctx->inputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->output_count < 1 || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const float* in = (const float*)ctx->inputs[0]->data;
    float* out = (float*)ctx->outputs[0]->data;
    uint32_t frames = ctx->frame_count;
    uint32_t n = frames * s->channels;
    for (uint32_t i = 0; i < n; ++i) out[i] = -in[i];
    if (ctx->outputs[0]->frame_capacity >= frames) ctx->outputs[0]->frame_count = frames;
    return ORPHEUS_OK;
}

static int neg_set(void* state, const char* id, const OrpheusValue* v) { (void)state;(void)id;(void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int neg_get(void* state, const char* id, OrpheusValue* v) {
    NegateState* s = (NegateState*)state;
    if (!strcmp(id, "channels")) { v->type=ORPHEUS_VALUE_INT; v->value.i32=(int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int neg_reg(void* state, const OrpheusRegistry* reg) {
    NegateState* s = (NegateState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "\u901a\u9053\u6570", ORPHEUS_VALUE_INT,
        .min_i32=1, .max_i32=64, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK|ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusParameter neg_params[] = {
    { .id="channels", .name="\u901a\u9053\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1}, .min_i32=1,.max_i32=64,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=true },
};
static const OrpheusPort neg_ports[] = {
    { .id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
    { .id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
};
static const OrpheusComponentDescriptor neg_desc = {
    .id="orpheus.builtin.negate", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=neg_ports, .port_count=2, .params=neg_params, .param_count=1,
    .state_size=sizeof(NegateState), .scratch_size=0, .alignment=8,
    .latency_samples=0, .realtime_safe=true, .supports_inplace=true
};
static const OrpheusComponentDescriptor* neg_get_desc(void){ return &neg_desc; }

static const OrpheusComponentInterface neg_iface = {
    .get_descriptor=neg_get_desc,.create=neg_create,.destroy=neg_destroy,
    .prepare=neg_prepare,.reset=neg_reset,.process=neg_process,
    .set_parameter=neg_set,.get_parameter=neg_get,
    .get_state_value=NULL,.register_slots=neg_reg
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void){ return &neg_iface; }
