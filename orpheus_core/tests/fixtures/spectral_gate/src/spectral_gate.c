#include "spectral_gate.h"

#include <string.h>

static const OrpheusParameter params[] = {
    {.id="channels", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .affects_signature=true},
    {.id="fft_size", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=256},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .affects_signature=true},
    {.id="num_frames", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .affects_signature=true}
};
static const OrpheusPort ports[] = {
    {.id="clock", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"},
    {.id="spectrum", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.test.spectral_gate", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .params=params, .param_count=3, .ports=ports, .port_count=3,
    .state_size=sizeof(SpectralGateState), .alignment=8, .realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    if (!state || !config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    *state = config->state_block;
    memset(*state, 0, sizeof(SpectralGateState));
    return ORPHEUS_OK;
}
static int destroy(void* state) { (void)state; return ORPHEUS_OK; }
static int reset(void* state) {
    ((SpectralGateState*)state)->calls = 0;
    return ORPHEUS_OK;
}
static int prepare(void* state, const OrpheusConfig* config) {
    SpectralGateState* s = (SpectralGateState*)state;
    s->channels = config->channels;
    s->quantum = config->block_size;
    s->fft_size = 256;
    s->num_frames = 1;
    s->calls = 0;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        const char* id = config->param_ids[i];
        uint32_t* target = NULL;
        if (id && strcmp(id, "fft_size") == 0) target = &s->fft_size;
        else if (id && strcmp(id, "num_frames") == 0) target = &s->num_frames;
        if (target) {
            if (config->param_values[i].type != ORPHEUS_VALUE_INT ||
                config->param_values[i].value.i32 <= 0) return ORPHEUS_ERR_INVALID_ARG;
            *target = (uint32_t)config->param_values[i].value.i32;
        }
    }
    return s->channels > 0 && s->channels <= 32 && s->fft_size <= 1024 &&
        s->num_frames <= 256 && s->quantum > 0 ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    SpectralGateState* s = (SpectralGateState*)state;
    if (!ctx || ctx->input_count < 2 || ctx->output_count < 1 ||
        !ctx->inputs[0] || !ctx->inputs[1] || !ctx->outputs[0])
        return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* clock = ctx->inputs[0];
    const OrpheusBuffer* in = ctx->inputs[1];
    OrpheusBuffer* out = ctx->outputs[0];
    const uint32_t frames = s->fft_size * s->num_frames;
    if (ctx->frame_count != s->quantum || ctx->frame_index != s->calls * s->quantum ||
        clock->frame_count != s->quantum || in->frame_count != frames ||
        !in->data || !out->data || in->channels != s->channels || out->channels != s->channels ||
        in->format != ORPHEUS_FORMAT_F32 || out->format != ORPHEUS_FORMAT_F32 ||
        !in->interleaved || !out->interleaved ||
        in->frame_capacity < frames || out->frame_capacity < frames)
        return ORPHEUS_ERR_INVALID_ARG;
    memcpy(out->data, in->data, (size_t)frames * s->channels * sizeof(float));
    out->frame_count = frames;
    ++s->calls;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    SpectralGateState* s = (SpectralGateState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
        ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, fft_size, ORPHEUS_SLOT_SETTING, "fft_size", "FFT点数",
        ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, num_frames, ORPHEUS_SLOT_SETTING, "num_frames", "批次帧数",
        ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_AFFECTS_SIGNATURE);
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
