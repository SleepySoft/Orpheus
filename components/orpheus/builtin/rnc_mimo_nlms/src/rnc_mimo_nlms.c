#include "orpheus_rnc_mimo_nlms.h"

#include <stdlib.h>
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

static uint32_t read_u32(const OrpheusConfig* config, const char* id, uint32_t fallback) {
    const OrpheusValue* value = find_param(config, id);
    if (value == NULL) return fallback;
    if (value->type == ORPHEUS_VALUE_INT) return (uint32_t)value->value.i32;
    if (value->type == ORPHEUS_VALUE_FLOAT) return (uint32_t)value->value.f32;
    return fallback;
}

static float read_f32(const OrpheusConfig* config, const char* id, float fallback) {
    const OrpheusValue* value = find_param(config, id);
    if (value == NULL) return fallback;
    if (value->type == ORPHEUS_VALUE_FLOAT) return value->value.f32;
    if (value->type == ORPHEUS_VALUE_INT) return (float)value->value.i32;
    return fallback;
}

static void parse_floats(const char* text, float* output, size_t count) {
    size_t index = 0;
    const char* cursor = text;
    if (text == NULL) return;
    while (index < count && *cursor != '\0') {
        char* end = NULL;
        float value = strtof(cursor, &end);
        if (end == cursor) {
            cursor++;
            continue;
        }
        output[index++] = value;
        cursor = end;
    }
}

static size_t weight_index(const RncMimoNlmsState* state, uint32_t output,
                           uint32_t reference, uint32_t tap) {
    return ((size_t)output * state->reference_channels + reference)
           * state->filter_length + tap;
}

static size_t history_index(const RncMimoNlmsState* state, uint32_t reference,
                            uint32_t position) {
    return (size_t)reference * state->filter_length + position;
}

static const OrpheusParameter parameters[] = {
    { .id="reference_channels", .name="\u53c2\u8003\u901a\u9053\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=12}, .min_i32=1,.max_i32=12,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=true },
    { .id="output_channels", .name="\u8f93\u51fa\u901a\u9053\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=8}, .min_i32=1,.max_i32=8,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=true },
    { .id="filter_length", .name="\u81ea\u9002\u5e94\u6ee4\u6ce2\u957f\u5ea6", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=125}, .min_i32=1,.max_i32=125,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true },
    { .id="step_sizes", .name="\u5404\u8f93\u51fa\u6b65\u957f", .type=ORPHEUS_VALUE_STRING,
      .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="0,0,0,0,0,0,0,0"},
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=false,.persistent=true },
    { .id="leakage", .name="\u6cc4\u6f0f\u56e0\u5b50", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.0f}, .min_f32=0.0f,.max_f32=1.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true },
    { .id="eps", .name="\u5f52\u4e00\u5316\u6b63\u5219\u9879", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.0e-5f}, .min_f32=1.0e-12f,.max_f32=1.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true },
    { .id="initial_weights", .name="\u521d\u59cb\u6743\u503c", .type=ORPHEUS_VALUE_STRING,
      .default_value={.type=ORPHEUS_VALUE_STRING,.value.str=""},
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=false,.persistent=true }
};

static const OrpheusPort ports[] = {
    { .id="ref", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
      .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0,
      .is_variable=true, .channels_param="reference_channels" },
    { .id="filtered_error", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
      .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0,
      .is_variable=true, .channels_param="output_channels" },
    { .id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO,
      .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0,
      .is_variable=true, .channels_param="output_channels" }
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.rnc_mimo_nlms", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports, .port_count=3, .params=parameters, .param_count=7,
    .state_size=sizeof(RncMimoNlmsState), .scratch_size=0, .alignment=8,
    .latency_samples=0, .realtime_safe=true, .supports_inplace=false
};

static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static int create_state(void** state, const OrpheusConfig* config) {
    if (state == NULL || config == NULL || config->state_block == NULL) return ORPHEUS_ERR_INVALID_ARG;
    *state = config->state_block;
    memset(*state, 0, sizeof(RncMimoNlmsState));
    return ORPHEUS_OK;
}

static int destroy_state(void* state) { (void)state; return ORPHEUS_OK; }

static int prepare_state(void* state, const OrpheusConfig* config) {
    RncMimoNlmsState* nlms = (RncMimoNlmsState*)state;
    const OrpheusValue* step_sizes;
    const OrpheusValue* initial_weights;
    memset(nlms, 0, sizeof(*nlms));
    nlms->reference_channels = read_u32(config, "reference_channels", 12);
    nlms->output_channels = read_u32(config, "output_channels", 8);
    nlms->filter_length = read_u32(config, "filter_length", 125);
    if (nlms->reference_channels < 1 || nlms->reference_channels > RNC_NLMS_MAX_REFERENCES ||
        nlms->output_channels < 1 || nlms->output_channels > RNC_NLMS_MAX_OUTPUTS ||
        nlms->filter_length < 1 || nlms->filter_length > RNC_NLMS_MAX_TAPS) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    nlms->leakage = read_f32(config, "leakage", 1.0f);
    nlms->eps = read_f32(config, "eps", 1.0e-5f);
    if (nlms->eps < 1.0e-12f) nlms->eps = 1.0e-12f;
    step_sizes = find_param(config, "step_sizes");
    if (step_sizes != NULL && step_sizes->type == ORPHEUS_VALUE_STRING) {
        parse_floats(step_sizes->value.str, nlms->step_sizes, nlms->output_channels);
    }
    initial_weights = find_param(config, "initial_weights");
    if (initial_weights != NULL && initial_weights->type == ORPHEUS_VALUE_STRING) {
        parse_floats(initial_weights->value.str, nlms->initial_weights,
                     (size_t)nlms->reference_channels * nlms->output_channels * nlms->filter_length);
    }
    memcpy(nlms->weights, nlms->initial_weights, sizeof(nlms->weights));
    return ORPHEUS_OK;
}

static int reset_state(void* state) {
    RncMimoNlmsState* nlms = (RncMimoNlmsState*)state;
    memcpy(nlms->weights, nlms->initial_weights, sizeof(nlms->weights));
    memset(nlms->history, 0, sizeof(nlms->history));
    nlms->position = 0;
    nlms->leakage_reference = 0;
    nlms->leakage_output = 0;
    return ORPHEUS_OK;
}

static int process_state(void* state, const OrpheusProcessContext* context) {
    RncMimoNlmsState* nlms = (RncMimoNlmsState*)state;
    const OrpheusBuffer* reference_buffer;
    const OrpheusBuffer* error_buffer;
    OrpheusBuffer* output_buffer;
    const float* reference;
    const float* filtered_error;
    float* output;
    uint32_t frame;
    uint32_t ref_index;
    uint32_t output_index;
    uint32_t tap;
    if (context == NULL || context->input_count < 2 || context->output_count < 1) return ORPHEUS_ERR_INVALID_ARG;
    reference_buffer = context->inputs[0];
    error_buffer = context->inputs[1];
    output_buffer = context->outputs[0];
    if (reference_buffer == NULL || error_buffer == NULL || output_buffer == NULL ||
        reference_buffer->data == NULL || error_buffer->data == NULL || output_buffer->data == NULL) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    reference = (const float*)reference_buffer->data;
    filtered_error = (const float*)error_buffer->data;
    output = (float*)output_buffer->data;

    if (nlms->leakage < 1.0f) {
        for (tap = 0; tap < nlms->filter_length; ++tap) {
            nlms->weights[weight_index(nlms, nlms->leakage_output,
                                       nlms->leakage_reference, tap)] *= nlms->leakage;
        }
        nlms->leakage_output++;
        if (nlms->leakage_output >= nlms->output_channels) {
            nlms->leakage_output = 0;
            nlms->leakage_reference = (nlms->leakage_reference + 1) % nlms->reference_channels;
        }
    }

    for (frame = 0; frame < context->frame_count; ++frame) {
        float norm = nlms->eps;
        for (ref_index = 0; ref_index < nlms->reference_channels; ++ref_index) {
            nlms->history[history_index(nlms, ref_index, nlms->position)] =
                reference[(size_t)frame * nlms->reference_channels + ref_index];
            for (tap = 0; tap < nlms->filter_length; ++tap) {
                uint32_t position = (nlms->position + nlms->filter_length - tap) % nlms->filter_length;
                float sample = nlms->history[history_index(nlms, ref_index, position)];
                norm += sample * sample;
            }
        }
        for (output_index = 0; output_index < nlms->output_channels; ++output_index) {
            float value = 0.0f;
            float scale = nlms->step_sizes[output_index]
                          * filtered_error[(size_t)frame * nlms->output_channels + output_index] / norm;
            for (ref_index = 0; ref_index < nlms->reference_channels; ++ref_index) {
                for (tap = 0; tap < nlms->filter_length; ++tap) {
                    uint32_t position = (nlms->position + nlms->filter_length - tap) % nlms->filter_length;
                    float sample = nlms->history[history_index(nlms, ref_index, position)];
                    size_t index = weight_index(nlms, output_index, ref_index, tap);
                    value += nlms->weights[index] * sample;
                    nlms->weights[index] += scale * sample;
                }
            }
            output[(size_t)frame * nlms->output_channels + output_index] = value;
        }
        nlms->position = (nlms->position + 1) % nlms->filter_length;
    }
    output_buffer->frame_count = context->frame_count;
    return ORPHEUS_OK;
}

static int set_parameter(void* state, const char* id, const OrpheusValue* value) {
    RncMimoNlmsState* nlms = (RncMimoNlmsState*)state;
    if (value == NULL) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(id, "leakage") == 0 && value->type == ORPHEUS_VALUE_FLOAT) {
        nlms->leakage = value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(id, "eps") == 0 && value->type == ORPHEUS_VALUE_FLOAT) {
        nlms->eps = value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int get_parameter(void* state, const char* id, OrpheusValue* value) {
    RncMimoNlmsState* nlms = (RncMimoNlmsState*)state;
    if (value == NULL) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(id, "reference_channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)nlms->reference_channels;
    } else if (strcmp(id, "output_channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)nlms->output_channels;
    } else if (strcmp(id, "filter_length") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)nlms->filter_length;
    } else if (strcmp(id, "leakage") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = nlms->leakage;
    } else if (strcmp(id, "eps") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = nlms->eps;
    } else {
        return ORPHEUS_ERR_NOT_FOUND;
    }
    return ORPHEUS_OK;
}

static int register_slots(void* state, const OrpheusRegistry* registry) {
    RncMimoNlmsState* nlms = (RncMimoNlmsState*)state;
    ORPHEUS_REG_SLOT(registry, nlms, reference_channels, ORPHEUS_SLOT_SETTING,
                     "reference_channels", "\u53c2\u8003\u901a\u9053\u6570", ORPHEUS_VALUE_INT,
                     .min_i32=1, .max_i32=12, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(registry, nlms, output_channels, ORPHEUS_SLOT_SETTING,
                     "output_channels", "\u8f93\u51fa\u901a\u9053\u6570", ORPHEUS_VALUE_INT,
                     .min_i32=1, .max_i32=8, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(registry, nlms, filter_length, ORPHEUS_SLOT_SETTING,
                     "filter_length", "\u81ea\u9002\u5e94\u6ee4\u6ce2\u957f\u5ea6", ORPHEUS_VALUE_INT,
                     .min_i32=1, .max_i32=125, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry, nlms, leakage, ORPHEUS_SLOT_SETTING,
                     "leakage", "\u6cc4\u6f0f\u56e0\u5b50", ORPHEUS_VALUE_FLOAT,
                     .min_f32=0.0f, .max_f32=1.0f, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry, nlms, eps, ORPHEUS_SLOT_SETTING,
                     "eps", "\u5f52\u4e00\u5316\u6b63\u5219\u9879", ORPHEUS_VALUE_FLOAT,
                     .min_f32=1.0e-12f, .max_f32=1.0f, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_ARRAY(registry, nlms, weights, RNC_NLMS_MAX_WEIGHTS, ORPHEUS_SLOT_BULK,
                      "weights", "\u81ea\u9002\u5e94\u6743\u503c", ORPHEUS_VALUE_FLOAT,
                      .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED);
    ORPHEUS_REG_ARRAY(registry, nlms, step_sizes, RNC_NLMS_MAX_OUTPUTS, ORPHEUS_SLOT_BULK,
                      "step_sizes", "\u5404\u8f93\u51fa\u6b65\u957f", ORPHEUS_VALUE_FLOAT);
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
