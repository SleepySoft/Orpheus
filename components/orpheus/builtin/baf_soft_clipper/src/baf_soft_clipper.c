#include "orpheus_baf_soft_clipper.h"

#include <math.h>
#include <string.h>

static const OrpheusValue* find_param(const OrpheusConfig* config, const char* id) {
    uint32_t index;
    for (index = 0; index < config->param_count; ++index) {
        if (config->param_ids[index] != NULL && strcmp(config->param_ids[index], id) == 0) {
            return &config->param_values[index];
        }
    }
    return NULL;
}

static float read_f32(const OrpheusConfig* config, const char* id, float fallback) {
    const OrpheusValue* value = find_param(config, id);
    if (value == NULL) return fallback;
    if (value->type == ORPHEUS_VALUE_FLOAT) return value->value.f32;
    if (value->type == ORPHEUS_VALUE_INT) return (float)value->value.i32;
    return fallback;
}

static uint32_t read_u32(const OrpheusConfig* config, const char* id, uint32_t fallback) {
    const OrpheusValue* value = find_param(config, id);
    if (value == NULL) return fallback;
    if (value->type == ORPHEUS_VALUE_INT) return (uint32_t)value->value.i32;
    if (value->type == ORPHEUS_VALUE_BOOL) return value->value.b ? 1U : 0U;
    return fallback;
}

#define FLOAT_PARAM(ID, NAME, DEFAULT) \
    { .id=ID, .name=NAME, .type=ORPHEUS_VALUE_FLOAT, \
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=DEFAULT}, \
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true }

static const OrpheusParameter parameters[] = {
    FLOAT_PARAM("xmin", "\u9ad8\u6863\u8d77\u59cb\u70b9", 0.65f),
    FLOAT_PARAM("xmax", "\u9ad8\u6863\u8f93\u5165\u4e0a\u9650", 1.35f),
    FLOAT_PARAM("p2", "\u9ad8\u6863\u4e8c\u6b21\u7cfb\u6570", 0.714285731f),
    FLOAT_PARAM("xmin_low", "\u4f4e\u6863\u8d77\u59cb\u70b9", 0.65f),
    FLOAT_PARAM("xmax_low", "\u4f4e\u6863\u8f93\u5165\u4e0a\u9650", 1.35f),
    FLOAT_PARAM("p2_low", "\u4f4e\u6863\u4e8c\u6b21\u7cfb\u6570", 0.714285731f),
    { .id="param_set", .name="\u53c2\u6570\u6863\u4f4d", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1}, .min_i32=0,.max_i32=1,
      .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true },
    { .id="disabled", .name="\u7981\u7528", .type=ORPHEUS_VALUE_BOOL,
    .default_value={.type=ORPHEUS_VALUE_BOOL,.value.b=false},
      .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true },
    { .id="channels", .name="\u901a\u9053\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=22}, .min_i32=1,.max_i32=32,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=true },
    { .id="active_mask", .name="\u6fc0\u6d3b\u901a\u9053\u4f4d\u56fe", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false }
};

static const OrpheusPort ports[] = {
    { .id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
      .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0,
      .is_variable=true, .channels_param="channels" },
    { .id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO,
      .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0,
      .is_variable=true, .channels_param="channels" }
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.baf_soft_clipper", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports, .port_count=2, .params=parameters, .param_count=10,
    .state_size=sizeof(BafSoftClipperState), .scratch_size=0, .alignment=8,
    .latency_samples=0, .realtime_safe=true, .supports_inplace=true
};

static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static int create_state(void** state, const OrpheusConfig* config) {
    if (state == NULL || config == NULL || config->state_block == NULL) return ORPHEUS_ERR_INVALID_ARG;
    *state = config->state_block;
    memset(*state, 0, sizeof(BafSoftClipperState));
    return ORPHEUS_OK;
}

static int destroy_state(void* state) { (void)state; return ORPHEUS_OK; }

static int prepare_state(void* state, const OrpheusConfig* config) {
    BafSoftClipperState* clipper = (BafSoftClipperState*)state;
    clipper->xmin = read_f32(config, "xmin", 0.65f);
    clipper->xmax = read_f32(config, "xmax", 1.35f);
    clipper->p2 = read_f32(config, "p2", 0.714285731f);
    clipper->xmin_low = read_f32(config, "xmin_low", 0.65f);
    clipper->xmax_low = read_f32(config, "xmax_low", 1.35f);
    clipper->p2_low = read_f32(config, "p2_low", 0.714285731f);
    clipper->param_set = read_u32(config, "param_set", 1);
    clipper->disabled = read_u32(config, "disabled", 0);
    clipper->channels = read_u32(config, "channels", config->channels > 0 ? config->channels : 22);
    clipper->active_mask = 0;
    if (clipper->param_set > 1 || clipper->channels < 1 || clipper->channels > 32 || clipper->xmin > clipper->xmax ||
        clipper->xmin_low > clipper->xmax_low) return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}

static int reset_state(void* state) {
    ((BafSoftClipperState*)state)->active_mask = 0;
    return ORPHEUS_OK;
}

static int process_state(void* state, const OrpheusProcessContext* context) {
    BafSoftClipperState* clipper = (BafSoftClipperState*)state;
    const OrpheusBuffer* input;
    OrpheusBuffer* output;
    const float* input_data;
    float* output_data;
    float xmin;
    float xmax;
    float p2;
    uint32_t channel;
    uint32_t frame;
    uint32_t active_mask = 0;
    if (context == NULL || context->input_count < 1 || context->output_count < 1) return ORPHEUS_ERR_INVALID_ARG;
    input = context->inputs[0];
    output = context->outputs[0];
    if (input == NULL || output == NULL || input->data == NULL || output->data == NULL) return ORPHEUS_ERR_INVALID_ARG;
    input_data = (const float*)input->data;
    output_data = (float*)output->data;
    xmin = clipper->param_set == 1 ? clipper->xmin : clipper->xmin_low;
    xmax = clipper->param_set == 1 ? clipper->xmax : clipper->xmax_low;
    p2 = clipper->param_set == 1 ? clipper->p2 : clipper->p2_low;
    for (channel = 0; channel < clipper->channels; ++channel) {
        float peak = 0.0f;
        for (frame = 0; frame < context->frame_count; ++frame) {
            size_t index = (size_t)frame * clipper->channels + channel;
            float sample = input_data[index];
            float magnitude = fabsf(sample);
            float limited = fminf(magnitude, xmax);
            float excess = fmaxf(limited - xmin, 0.0f);
            output_data[index] = clipper->disabled ? sample : copysignf(limited - excess * excess * p2, sample);
            if (magnitude > peak) peak = magnitude;
        }
        if (!clipper->disabled && peak > xmin) active_mask |= (uint32_t)1U << channel;
    }
    clipper->active_mask = (int32_t)active_mask;
    output->frame_count = context->frame_count;
    return ORPHEUS_OK;
}

static int set_parameter(void* state, const char* id, const OrpheusValue* value) {
    BafSoftClipperState* clipper = (BafSoftClipperState*)state;
    if (value == NULL) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(id, "param_set") == 0 && value->type == ORPHEUS_VALUE_INT) {
        if (value->value.i32 < 0 || value->value.i32 > 1) return ORPHEUS_ERR_INVALID_ARG;
        clipper->param_set = value->value.i32 != 0;
    } else if (strcmp(id, "disabled") == 0 && value->type == ORPHEUS_VALUE_BOOL) {
        clipper->disabled = value->value.b ? 1U : 0U;
    } else {
        return ORPHEUS_ERR_UNSUPPORTED;
    }
    return ORPHEUS_OK;
}

static int get_parameter(void* state, const char* id, OrpheusValue* value) {
    BafSoftClipperState* clipper = (BafSoftClipperState*)state;
    if (value == NULL) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(id, "active_mask") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = clipper->active_mask;
    } else if (strcmp(id, "param_set") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)clipper->param_set;
    } else if (strcmp(id, "disabled") == 0) {
        value->type = ORPHEUS_VALUE_BOOL; value->value.b = clipper->disabled != 0;
    } else if (strcmp(id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)clipper->channels;
    } else {
        return ORPHEUS_ERR_NOT_FOUND;
    }
    return ORPHEUS_OK;
}

static int register_slots(void* state, const OrpheusRegistry* registry) {
    BafSoftClipperState* clipper = (BafSoftClipperState*)state;
    ORPHEUS_REG_SLOT(registry, clipper, param_set, ORPHEUS_SLOT_SETTING, "param_set",
                     "\u53c2\u6570\u6863\u4f4d", ORPHEUS_VALUE_INT, .min_i32=0, .max_i32=1,
                     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(registry, clipper, disabled, ORPHEUS_SLOT_SETTING, "disabled",
                     "\u7981\u7528", ORPHEUS_VALUE_BOOL,
                     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(registry, clipper, active_mask, ORPHEUS_SLOT_PROBE, "active_mask",
                     "\u6fc0\u6d3b\u901a\u9053\u4f4d\u56fe", ORPHEUS_VALUE_INT,
                     .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry, clipper, channels, ORPHEUS_SLOT_SETTING, "channels",
                     "\u901a\u9053\u6570", ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface interface = {
    get_descriptor, create_state, destroy_state, prepare_state, reset_state, process_state,
    set_parameter, get_parameter, NULL, register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
