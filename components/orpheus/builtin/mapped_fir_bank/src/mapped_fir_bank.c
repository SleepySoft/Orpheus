#include "orpheus_mapped_fir_bank.h"

#include <stdlib.h>
#include <string.h>

static int32_t read_int(const OrpheusConfig* config, const char* id, int32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int32_t)config->param_values[i].value.f32;
        }
    }
    return fallback;
}

static const char* read_string(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_STRING) {
            return config->param_values[i].value.str;
        }
    }
    return fallback;
}

static uint32_t parse_uints(const char* text, uint32_t* output, uint32_t capacity) {
    uint32_t count = 0;
    const char* cursor = text;
    while (cursor && *cursor && count < capacity) {
        char* end = NULL;
        unsigned long value = strtoul(cursor, &end, 10);
        if (end == cursor) {
            ++cursor;
            continue;
        }
        output[count++] = (uint32_t)value;
        cursor = end;
    }
    return count;
}

static uint32_t parse_floats(const char* text, float* output, uint32_t capacity) {
    uint32_t count = 0;
    const char* cursor = text;
    while (cursor && *cursor && count < capacity) {
        char* end = NULL;
        float value = strtof(cursor, &end);
        if (end == cursor) {
            ++cursor;
            continue;
        }
        output[count++] = value;
        cursor = end;
    }
    return count;
}

static void release_buffers(MappedFirBankState* state) {
    free(state->coefficients);
    free(state->history);
    free(state->output_delay);
    state->coefficients = NULL;
    state->history = NULL;
    state->output_delay = NULL;
    state->coefficient_count = 0;
}

static const OrpheusParameter mapped_fir_params[] = {
    { .id="input_channels", .name="Input Channels", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2}, .min_i32=1, .max_i32=64, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .readback=true, .persistent=true, .affects_signature=true },
    { .id="filter_count", .name="Filter Count", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2}, .min_i32=1, .max_i32=64, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .readback=true, .persistent=true },
    { .id="output_channels", .name="Output Channels", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2}, .min_i32=1, .max_i32=64, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .readback=true, .persistent=true, .affects_signature=true },
    { .id="filter_lengths", .name="Filter Lengths", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="1,1"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .persistent=true },
    { .id="coefficient_mapping", .name="Coefficient Mapping", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="0,1"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .persistent=true },
    { .id="input_mapping", .name="Input Mapping", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="0,1"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .persistent=true },
    { .id="output_starts", .name="Output Starts", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="0,1"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .persistent=true },
    { .id="output_delay_samples", .name="Output Delay Samples", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0}, .min_i32=0, .max_i32=4096, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .persistent=true },
    { .id="coefficients", .name="Coefficients", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="1,1"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .persistent=true },
    { .id="max_taps", .name="Max Taps", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .readback=true }
};

static const OrpheusPort mapped_fir_ports[] = {
    { .id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="input_channels" },
    { .id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="output_channels" }
};

static const OrpheusComponentDescriptor mapped_fir_descriptor = {
    .id="orpheus.builtin.mapped_fir_bank", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=mapped_fir_ports, .port_count=2, .params=mapped_fir_params, .param_count=10,
    .state_size=sizeof(MappedFirBankState), .scratch_size=0, .alignment=8,
    .latency_samples=0, .realtime_safe=true, .supports_inplace=false
};

static const OrpheusComponentDescriptor* get_descriptor(void) { return &mapped_fir_descriptor; }

static int create_state(void** state, const OrpheusConfig* config) {
    if (config && config->state_block) {
        *state = config->state_block;
        memset(*state, 0, sizeof(MappedFirBankState));
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(MappedFirBankState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int destroy_state(void* state) {
    release_buffers((MappedFirBankState*)state);
    return ORPHEUS_OK;
}

static int prepare_state(void* opaque, const OrpheusConfig* config) {
    MappedFirBankState* state = (MappedFirBankState*)opaque;
    release_buffers(state);
    state->input_channels = (uint32_t)read_int(config, "input_channels", 2);
    state->filter_count = (uint32_t)read_int(config, "filter_count", 2);
    state->output_channels = (uint32_t)read_int(config, "output_channels", 2);
    state->output_delay_samples = (uint32_t)read_int(config, "output_delay_samples", 0);
    if (state->input_channels < 1 || state->input_channels > MAPPED_FIR_MAX_INPUTS ||
        state->filter_count < 1 || state->filter_count > MAPPED_FIR_MAX_FILTERS ||
        state->output_channels < 1 || state->output_channels > MAPPED_FIR_MAX_OUTPUTS ||
        state->output_delay_samples > MAPPED_FIR_MAX_TAPS) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    if (parse_uints(read_string(config, "filter_lengths", ""), state->filter_lengths, state->filter_count) != state->filter_count ||
        parse_uints(read_string(config, "coefficient_mapping", ""), state->coefficient_mapping, state->filter_count) != state->filter_count ||
        parse_uints(read_string(config, "input_mapping", ""), state->input_mapping, state->filter_count) != state->filter_count ||
        parse_uints(read_string(config, "output_starts", ""), state->output_starts, state->output_channels) != state->output_channels) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    uint32_t coefficient_count = 0;
    state->max_taps = 0;
    for (uint32_t filter = 0; filter < state->filter_count; ++filter) {
        uint32_t length = state->filter_lengths[filter];
        if (length < 1 || length > MAPPED_FIR_MAX_TAPS ||
            state->coefficient_mapping[filter] >= state->filter_count ||
            state->input_mapping[filter] >= state->input_channels) {
            return ORPHEUS_ERR_INVALID_ARG;
        }
        state->coefficient_offsets[filter] = coefficient_count;
        coefficient_count += length;
        if (length > state->max_taps) state->max_taps = length;
    }
    for (uint32_t filter = 0; filter < state->filter_count; ++filter) {
        if (state->filter_lengths[state->coefficient_mapping[filter]] !=
            state->filter_lengths[filter]) {
            return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    if (state->output_starts[0] != 0) return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t output = 1; output < state->output_channels; ++output) {
        if (state->output_starts[output] <= state->output_starts[output - 1] ||
            state->output_starts[output] >= state->filter_count) {
            return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    state->coefficients = (float*)malloc((size_t)coefficient_count * sizeof(float));
    state->history = (float*)calloc(
        (size_t)state->input_channels * state->max_taps, sizeof(float)
    );
    if (state->output_delay_samples > 0) {
        state->output_delay = (float*)calloc(
            (size_t)state->output_channels * state->output_delay_samples,
            sizeof(float)
        );
    }
    if (!state->coefficients || !state->history ||
        (state->output_delay_samples > 0 && !state->output_delay)) {
        release_buffers(state);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    uint32_t parsed = parse_floats(
        read_string(config, "coefficients", ""), state->coefficients, coefficient_count
    );
    if (parsed != coefficient_count) {
        release_buffers(state);
        return ORPHEUS_ERR_INVALID_ARG;
    }
    state->coefficient_count = coefficient_count;
    memset(state->positions, 0, sizeof(state->positions));
    state->output_delay_position = 0;
    return ORPHEUS_OK;
}

static int reset_state(void* opaque) {
    MappedFirBankState* state = (MappedFirBankState*)opaque;
    if (state->history) {
        memset(state->history, 0,
               (size_t)state->input_channels * state->max_taps * sizeof(float));
    }
    memset(state->positions, 0, sizeof(state->positions));
    if (state->output_delay) {
        memset(state->output_delay, 0,
               (size_t)state->output_channels * state->output_delay_samples * sizeof(float));
    }
    state->output_delay_position = 0;
    return ORPHEUS_OK;
}

static int process_state(void* opaque, const OrpheusProcessContext* ctx) {
    MappedFirBankState* state = (MappedFirBankState*)opaque;
    const OrpheusBuffer* input = ctx->inputs[0];
    OrpheusBuffer* output = ctx->outputs[0];
    if (!input || !output || !state->coefficients || !state->history) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    const float* in = (const float*)input->data;
    float* out = (float*)output->data;
    for (uint32_t frame = 0; frame < ctx->frame_count; ++frame) {
        for (uint32_t channel = 0; channel < state->input_channels; ++channel) {
            uint32_t position = state->positions[channel];
            state->history[(size_t)channel * state->max_taps + position] =
                in[(size_t)frame * state->input_channels + channel];
        }
        float* frame_out = &out[(size_t)frame * state->output_channels];
        memset(frame_out, 0, state->output_channels * sizeof(float));
        uint32_t output_index = 0;
        for (uint32_t filter = 0; filter < state->filter_count; ++filter) {
            while (output_index + 1 < state->output_channels &&
                   filter >= state->output_starts[output_index + 1]) {
                ++output_index;
            }
            uint32_t input_channel = state->input_mapping[filter];
            uint32_t length = state->filter_lengths[filter];
            uint32_t coefficient_set = state->coefficient_mapping[filter];
            uint32_t coefficient_offset = state->coefficient_offsets[coefficient_set];
            uint32_t position = state->positions[input_channel];
            const float* history = &state->history[(size_t)input_channel * state->max_taps];
            float value = 0.0f;
            for (uint32_t tap = 0; tap < length; ++tap) {
                uint32_t history_index = (position + state->max_taps - tap) % state->max_taps;
                value += state->coefficients[coefficient_offset + tap] * history[history_index];
            }
            frame_out[output_index] += value;
        }
        if (state->output_delay_samples > 0) {
            for (uint32_t channel = 0; channel < state->output_channels; ++channel) {
                size_t delay_index =
                    (size_t)channel * state->output_delay_samples +
                    state->output_delay_position;
                float delayed = state->output_delay[delay_index];
                state->output_delay[delay_index] = frame_out[channel];
                frame_out[channel] = delayed;
            }
            state->output_delay_position =
                (state->output_delay_position + 1) % state->output_delay_samples;
        }
        for (uint32_t channel = 0; channel < state->input_channels; ++channel) {
            state->positions[channel] = (state->positions[channel] + 1) % state->max_taps;
        }
    }
    output->frame_count = ctx->frame_count;
    output->interleaved = true;
    return ORPHEUS_OK;
}

static int get_parameter(void* opaque, const char* id, OrpheusValue* value) {
    MappedFirBankState* state = (MappedFirBankState*)opaque;
    value->type = ORPHEUS_VALUE_INT;
    if (strcmp(id, "input_channels") == 0) value->value.i32 = (int32_t)state->input_channels;
    else if (strcmp(id, "filter_count") == 0) value->value.i32 = (int32_t)state->filter_count;
    else if (strcmp(id, "output_channels") == 0) value->value.i32 = (int32_t)state->output_channels;
    else if (strcmp(id, "max_taps") == 0) value->value.i32 = (int32_t)state->max_taps;
    else if (strcmp(id, "output_delay_samples") == 0) value->value.i32 = (int32_t)state->output_delay_samples;
    else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}

static int set_parameter(void* opaque, const char* id, const OrpheusValue* value) {
    (void)opaque;
    (void)id;
    (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int register_slots(void* opaque, const OrpheusRegistry* registry) {
    MappedFirBankState* state = (MappedFirBankState*)opaque;
    ORPHEUS_REG_SLOT(registry, state, input_channels, ORPHEUS_SLOT_SETTING,
                     "input_channels", "输入通道数", ORPHEUS_VALUE_INT,
                     .min_i32=1, .max_i32=64,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(registry, state, filter_count, ORPHEUS_SLOT_SETTING,
                     "filter_count", "滤波器数量", ORPHEUS_VALUE_INT,
                     .min_i32=1, .max_i32=64,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry, state, output_channels, ORPHEUS_SLOT_SETTING,
                     "output_channels", "输出通道数", ORPHEUS_VALUE_INT,
                     .min_i32=1, .max_i32=64,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(registry, state, max_taps, ORPHEUS_SLOT_PROBE,
                     "max_taps", "最大抽头数", ORPHEUS_VALUE_INT,
                     .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry, state, output_delay_samples, ORPHEUS_SLOT_SETTING,
                     "output_delay_samples", "输出统一延迟", ORPHEUS_VALUE_INT,
                     .min_i32=0, .max_i32=4096,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface mapped_fir_interface = {
    .get_descriptor=get_descriptor, .create=create_state, .destroy=destroy_state,
    .prepare=prepare_state, .reset=reset_state, .process=process_state,
    .set_parameter=set_parameter, .get_parameter=get_parameter,
    .get_state_value=NULL, .register_slots=register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &mapped_fir_interface;
}