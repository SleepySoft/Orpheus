#include "orpheus_rational_resample.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2},
     .min_i32=1, .max_i32=64, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true},
    {.id="up", .name="插值因子", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=3},
     .min_i32=1, .max_i32=512, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true},
    {.id="down", .name="抽取因子", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=13},
     .min_i32=1, .max_i32=512, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true},
    {.id="taps", .name="每相滤波抽头数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=128},
     .min_i32=16, .max_i32=1024, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true}
};

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"}
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.rational_resample", .version="1.0.0",
    .abi_version=ORPHEUS_ABI_VERSION, .ports=ports, .port_count=2,
    .params=params, .param_count=4, .state_size=sizeof(RationalResampleState),
    .alignment=8, .realtime_safe=true
};

static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static int create(void** state, const OrpheusConfig* config) {
    if (config && config->state_block) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(RationalResampleState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int destroy(void* state) {
    RationalResampleState* s = (RationalResampleState*)state;
    free(s->coefficients);
    free(s->history);
    s->coefficients = NULL;
    s->history = NULL;
    return ORPHEUS_OK;
}

static int reset(void* state) {
    RationalResampleState* s = (RationalResampleState*)state;
    if (s->history) memset(s->history, 0, (size_t)s->taps * s->channels * sizeof(float));
    s->cursor = 0;
    return ORPHEUS_OK;
}

static int prepare(void* state, const OrpheusConfig* config) {
    RationalResampleState* s = (RationalResampleState*)state;
    if (!config) return ORPHEUS_ERR_INVALID_ARG;
    destroy(state);
    s->channels = config->channels;
    s->up = 3;
    s->down = 13;
    s->taps = 128;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        const char* id = config->param_ids[i];
        if (!id) continue;
        uint32_t* target = NULL;
        if (strcmp(id, "up") == 0) target = &s->up;
        else if (strcmp(id, "down") == 0) target = &s->down;
        else if (strcmp(id, "taps") == 0) target = &s->taps;
        if (target) {
            if (config->param_values[i].type != ORPHEUS_VALUE_INT ||
                config->param_values[i].value.i32 <= 0) return ORPHEUS_ERR_INVALID_ARG;
            *target = (uint32_t)config->param_values[i].value.i32;
        }
    }
    if (s->channels < 1 || s->channels > 64 || s->up > 512 || s->down > 512 ||
        s->taps < 16 || s->taps > 1024 ||
        (uint64_t)config->block_size * s->up % s->down != 0 ||
        config->block_size == 0) return ORPHEUS_ERR_INVALID_ARG;

    s->coefficients = (float*)calloc((size_t)s->up * s->taps, sizeof(float));
    s->history = (float*)calloc((size_t)s->channels * s->taps, sizeof(float));
    if (!s->coefficients || !s->history) {
        destroy(state);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    const double pi = 3.14159265358979323846;
    const double cutoff = 0.45 * fmin(1.0, (double)s->up / s->down);
    const double delay = 0.5 * (s->taps - 1);
    for (uint32_t phase = 0; phase < s->up; ++phase) {
        double sum = 0.0;
        for (uint32_t tap = 0; tap < s->taps; ++tap) {
            const double x = tap - delay + (double)phase / s->up;
            const double angle = 2.0 * pi * cutoff * x;
            const double sinc = fabs(angle) < 1e-12 ? 1.0 : sin(angle) / angle;
            const double w = 2.0 * pi * tap / (s->taps - 1);
            const double window = 0.42 - 0.5 * cos(w) + 0.08 * cos(2.0 * w);
            const double value = 2.0 * cutoff * sinc * window;
            s->coefficients[(size_t)phase * s->taps + tap] = (float)value;
            sum += value;
        }
        for (uint32_t tap = 0; tap < s->taps; ++tap)
            s->coefficients[(size_t)phase * s->taps + tap] /= (float)sum;
    }
    return reset(state);
}

static void push(RationalResampleState* s, const float* input) {
    memcpy(s->history + (size_t)s->cursor * s->channels, input,
           s->channels * sizeof(float));
    s->cursor = (s->cursor + 1) % s->taps;
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    RationalResampleState* s = (RationalResampleState*)state;
    if (!ctx || ctx->input_count < 1 || ctx->output_count < 1 ||
        !ctx->inputs[0] || !ctx->outputs[0] || !s->history) return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    const uint64_t count = (uint64_t)ctx->frame_count * s->up;
    if (!in->data || !out->data || in->channels != s->channels ||
        out->channels != s->channels || !in->interleaved || !out->interleaved ||
        in->format != ORPHEUS_FORMAT_F32 || out->format != ORPHEUS_FORMAT_F32 ||
        ctx->frame_count > in->frame_capacity || count % s->down != 0 ||
        count / s->down > out->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
    const float* input = (const float*)in->data;
    float* output = (float*)out->data;
    const uint32_t output_frames = (uint32_t)(count / s->down);
    if (s->up == s->down) {
        memcpy(output, input, (size_t)output_frames * s->channels * sizeof(float));
        out->frame_count = output_frames;
        return ORPHEUS_OK;
    }
    uint32_t consumed = 0;
    for (uint32_t f = 0; f < output_frames; ++f) {
        const uint64_t position = (uint64_t)f * s->down;
        const uint32_t sample = (uint32_t)(position / s->up);
        const uint32_t phase = (uint32_t)(position % s->up);
        while (consumed <= sample)
            push(s, input + (size_t)consumed++ * s->channels);
        for (uint32_t ch = 0; ch < s->channels; ++ch) {
            double value = 0.0;
            for (uint32_t tap = 0; tap < s->taps; ++tap) {
                const uint32_t slot = (s->cursor + s->taps - 1 - tap) % s->taps;
                value += s->history[(size_t)slot * s->channels + ch] *
                    (double)s->coefficients[(size_t)phase * s->taps + tap];
            }
            output[(size_t)f * s->channels + ch] = (float)value;
        }
    }
    while (consumed < ctx->frame_count)
        push(s, input + (size_t)consumed++ * s->channels);
    out->frame_count = output_frames;
    return ORPHEUS_OK;
}

static int register_slots(void* state, const OrpheusRegistry* reg) {
    RationalResampleState* s = (RationalResampleState*)state;
#define REGISTER(field, label, lo, hi, flags_) \
    ORPHEUS_REG_SLOT(reg, s, field, ORPHEUS_SLOT_SETTING, #field, label, ORPHEUS_VALUE_INT, \
        .min_i32=lo, .max_i32=hi, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, \
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | flags_)
    REGISTER(channels, "通道数", 1, 64, ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    REGISTER(up, "插值因子", 1, 512, ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    REGISTER(down, "抽取因子", 1, 512, ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    REGISTER(taps, "每相滤波抽头数", 16, 1024, 0);
#undef REGISTER
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface interface_ = {
    .get_descriptor=get_descriptor, .create=create, .destroy=destroy,
    .prepare=prepare, .reset=reset, .process=process, .register_slots=register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface_; }
