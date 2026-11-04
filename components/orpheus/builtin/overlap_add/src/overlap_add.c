#include "orpheus_overlap_add.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2},
     .min_i32=1, .max_i32=64, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true},
    {.id="frame_size", .name="帧长", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=128},
     .min_i32=2, .max_i32=4096, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true},
    {.id="hop_size", .name="帧移", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=64},
     .min_i32=1, .max_i32=4096, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true},
    {.id="num_frames", .name="每块输入帧数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=12},
     .min_i32=1, .max_i32=256, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true}
};
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.overlap_add", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports, .port_count=2, .params=params, .param_count=4,
    .state_size=sizeof(OverlapAddState), .alignment=8,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    if (config && config->state_block) {
        *state = config->state_block;
        memset(*state, 0, sizeof(OverlapAddState));
    } else {
        *state = calloc(1, sizeof(OverlapAddState));
        if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
        ((OverlapAddState*)*state)->owns_state = true;
    }
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    OverlapAddState* s = (OverlapAddState*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    free(s->history);
    s->history = NULL;
    if (s->owns_state) free(s);
    return ORPHEUS_OK;
}
static int reset(void* state) {
    OverlapAddState* s = (OverlapAddState*)state;
    if (!s || !s->history) return ORPHEUS_ERR_INVALID_ARG;
    memset(s->history, 0, (size_t)s->frame_size * s->channels * sizeof(float));
    return ORPHEUS_OK;
}
static int prepare(void* state, const OrpheusConfig* config) {
    OverlapAddState* s = (OverlapAddState*)state;
    if (!s || !config) return ORPHEUS_ERR_INVALID_ARG;
    free(s->history);
    s->history = NULL;
    s->channels = config->channels ? config->channels : 2;
    s->frame_size = 128;
    s->hop_size = 64;
    s->num_frames = 12;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        const char* id = config->param_ids[i];
        uint32_t* target = NULL;
        if (id && strcmp(id, "frame_size") == 0) target = &s->frame_size;
        else if (id && strcmp(id, "hop_size") == 0) target = &s->hop_size;
        else if (id && strcmp(id, "num_frames") == 0) target = &s->num_frames;
        if (target) {
            if (config->param_values[i].type != ORPHEUS_VALUE_INT ||
                config->param_values[i].value.i32 <= 0) return ORPHEUS_ERR_INVALID_ARG;
            *target = (uint32_t)config->param_values[i].value.i32;
        }
    }
    if (s->channels > 64 || s->frame_size < 2 || s->frame_size > 4096 ||
        s->hop_size < 1 || s->hop_size > s->frame_size ||
        s->num_frames < 1 || s->num_frames > 256 ||
        config->block_size != s->frame_size * s->num_frames)
        return ORPHEUS_ERR_INVALID_ARG;
    s->history = (float*)calloc((size_t)s->frame_size * s->channels, sizeof(float));
    return s->history ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    OverlapAddState* s = (OverlapAddState*)state;
    if (!s || !s->history || !ctx || !ctx->inputs || !ctx->outputs ||
        ctx->input_count < 1 || ctx->output_count < 1 ||
        !ctx->inputs[0] || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    const uint32_t input_frames = s->frame_size * s->num_frames;
    const uint32_t output_frames = s->hop_size * s->num_frames;
    if (!in->data || !out->data || in->format != ORPHEUS_FORMAT_F32 ||
        out->format != ORPHEUS_FORMAT_F32 || !in->interleaved || !out->interleaved ||
        in->channels != s->channels || out->channels != s->channels ||
        in->frame_count != input_frames || in->frame_capacity < input_frames ||
        out->frame_capacity < output_frames) return ORPHEUS_ERR_INVALID_ARG;
    const size_t frame_count = (size_t)s->frame_size * s->channels;
    const size_t hop_count = (size_t)s->hop_size * s->channels;
    const size_t tail_count = frame_count - hop_count;
    for (uint32_t frame = 0; frame < s->num_frames; ++frame) {
        const float* src = (const float*)in->data + frame * frame_count;
        float* dst = (float*)out->data + frame * hop_count;
        for (size_t i = 0; i < frame_count; ++i) s->history[i] += src[i];
        memcpy(dst, s->history, hop_count * sizeof(float));
        if (tail_count) memmove(s->history, s->history + hop_count, tail_count * sizeof(float));
        memset(s->history + tail_count, 0, hop_count * sizeof(float));
    }
    out->frame_count = output_frames;
    return orpheus_process_next(ctx);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    OverlapAddState* s = (OverlapAddState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
        ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=64,
        .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, frame_size, ORPHEUS_SLOT_SETTING, "frame_size", "帧长",
        ORPHEUS_VALUE_INT, .min_i32=2, .max_i32=4096,
        .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, hop_size, ORPHEUS_SLOT_SETTING, "hop_size", "帧移",
        ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=4096,
        .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, num_frames, ORPHEUS_SLOT_SETTING, "num_frames", "每块输入帧数",
        ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=256,
        .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
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
