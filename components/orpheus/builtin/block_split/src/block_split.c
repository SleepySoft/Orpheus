#include "orpheus_block_split.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2},
     .min_i32=1, .max_i32=64, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true},
    {.id="factor", .name="拆分份数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=4},
     .min_i32=1, .max_i32=64, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true}
};
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.block_split", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports, .port_count=2, .params=params, .param_count=2,
    .state_size=sizeof(BlockSplitState), .alignment=8, .realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    if (config && config->state_block) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(BlockSplitState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) {
    BlockSplitState* s = (BlockSplitState*)state;
    free(s->block);
    s->block = NULL;
    return ORPHEUS_OK;
}
static int reset(void* state) {
    BlockSplitState* s = (BlockSplitState*)state;
    s->calls = 0;
    s->cursor = 0;
    s->started = false;
    if (s->block) memset(s->block, 0, (size_t)s->input_frames * s->channels * sizeof(float));
    return ORPHEUS_OK;
}
static int prepare(void* state, const OrpheusConfig* config) {
    BlockSplitState* s = (BlockSplitState*)state;
    if (!config) return ORPHEUS_ERR_INVALID_ARG;
    destroy(state);
    s->channels = config->channels;
    s->factor = 4;
    s->input_frames = config->block_size;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "factor") == 0) {
            if (config->param_values[i].type != ORPHEUS_VALUE_INT ||
                config->param_values[i].value.i32 < 1) return ORPHEUS_ERR_INVALID_ARG;
            s->factor = (uint32_t)config->param_values[i].value.i32;
        }
    }
    if (s->channels < 1 || s->channels > 64 || s->factor > 64 ||
        s->input_frames == 0 || s->input_frames % s->factor != 0)
        return ORPHEUS_ERR_INVALID_ARG;
    s->output_frames = s->input_frames / s->factor;
    s->block = (float*)calloc((size_t)s->input_frames * s->channels, sizeof(float));
    if (!s->block) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return reset(state);
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    BlockSplitState* s = (BlockSplitState*)state;
    if (!ctx || ctx->input_count < 1 || ctx->output_count < 1 ||
        !ctx->inputs[0] || !ctx->outputs[0] || !s->block) return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in->data || !out->data || in->channels != s->channels ||
        out->channels != s->channels || !in->interleaved || !out->interleaved ||
        in->format != ORPHEUS_FORMAT_F32 || out->format != ORPHEUS_FORMAT_F32 ||
        in->frame_capacity < s->input_frames || out->frame_capacity < s->output_frames ||
        ctx->frame_count != s->output_frames) return ORPHEUS_ERR_INVALID_ARG;

    /* 上游大块在同源周期末就绪；先快照，避免其下一批局部写入覆盖待读数据。 */
    if (++s->calls == s->factor) {
        s->calls = 0;
        memcpy(s->block, in->data, (size_t)s->input_frames * s->channels * sizeof(float));
        s->cursor = 0;
        s->started = true;
    }
    const size_t count = (size_t)s->output_frames * s->channels;
    if (s->started) {
        memcpy(out->data, s->block + (size_t)s->cursor * s->channels, count * sizeof(float));
        s->cursor += s->output_frames;
    } else {
        memset(out->data, 0, count * sizeof(float));
    }
    out->frame_count = s->output_frames;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    BlockSplitState* s = (BlockSplitState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
        ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=64,
        .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, factor, ORPHEUS_SLOT_SETTING, "factor", "拆分份数",
        ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=64,
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
