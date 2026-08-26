\
#include "orpheus_rate_sync.h"

#include <stdlib.h>
#include <string.h>

static const char* read_str(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_STRING) {
                const char* s = config->param_values[i].value.str;
                return s ? s : fallback;
            }
        }
    }
    return fallback;
}

static long read_param_i32(const OrpheusConfig* config, const char* id, long fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) {
                return (long)config->param_values[i].value.i32;
            }
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {
                return (long)config->param_values[i].value.f32;
            }
        }
    }
    return fallback;
}

static const OrpheusParameter rs_params[] = {
    { .id = "channels", .name = "\u901a\u9053\u6570", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 1 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "mode", .name = "\u7f13\u51b2\u6a21\u5f0f", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = 1,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "buffer_length", .name = "\u7f13\u51b2\u957f\u5ea6", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = RATE_SYNC_MAX_BUFFER,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort rs_ports[] = {
    { .id = "in0", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "in1", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor rs_desc = {
    .id = "orpheus.builtin.rate_sync", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = rs_ports, .port_count = 3, .params = rs_params, .param_count = 3,
    .state_size = sizeof(RateSyncState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* rs_get_descriptor(void) { return &rs_desc; }

static void rs_free(RateSyncState* s) {
    for (int i = 0; i < RATE_SYNC_MAX_INPUTS; ++i) {
        free(s->fifo[i]);
        s->fifo[i] = NULL;
    }
}

static int rs_create(void** state, const OrpheusConfig* config) {
    (void)config;
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(RateSyncState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int rs_destroy(void* state) {
    rs_free((RateSyncState*)state);
    return ORPHEUS_OK;
}

static int rs_prepare(void* state, const OrpheusConfig* config) {
    RateSyncState* s = (RateSyncState*)state;
    rs_free(s);

    s->channels = config->channels > 0 ? config->channels : 1;
    if (s->channels > 32) s->channels = 32;

    long mode_val = read_param_i32(config, "mode", 0);
    long fixed_len = read_param_i32(config, "buffer_length", 0);
    s->mode = (mode_val != 0) ? 1u : 0u;
    s->buffer_length = fixed_len > 0 ? (uint32_t)fixed_len : 0u;

    if (s->mode == 1u && s->buffer_length > 0) {
        s->align = s->buffer_length;
        if (s->align > RATE_SYNC_MAX_BUFFER) s->align = RATE_SYNC_MAX_BUFFER;
    } else {
        /* auto: 对齐窗口 = 编译器设定的节点块（LCM） */
        s->align = config->block_size > 0 ? config->block_size : 1;
        if (s->align > RATE_SYNC_MAX_BUFFER) s->align = RATE_SYNC_MAX_BUFFER;
    }

    for (int i = 0; i < RATE_SYNC_MAX_INPUTS; ++i) {
        size_t n = (size_t)s->channels * s->align;
        s->fifo[i] = (float*)calloc(n, sizeof(float));
        if (!s->fifo[i]) { rs_free(s); return ORPHEUS_ERR_OUT_OF_MEMORY; }
        s->wpos[i] = s->rpos[i] = 0;
        s->stored[i] = 0;
        s->init_block[i] = 0;
    }
    return ORPHEUS_OK;
}

static int rs_reset(void* state) {
    RateSyncState* s = (RateSyncState*)state;
    for (int i = 0; i < RATE_SYNC_MAX_INPUTS; ++i) {
        if (s->fifo[i] && s->align > 0) {
            memset(s->fifo[i], 0, (size_t)s->channels * s->align * sizeof(float));
        }
        s->wpos[i] = s->rpos[i] = 0;
        s->stored[i] = 0;
    }
    return ORPHEUS_OK;
}

/* 向指定 FIFO 追加 frames 帧（交错格式 in=channels*frames 已保证）。 */
static void fifo_append(RateSyncState* s, int which, const float* src, uint32_t frames) {
    uint32_t cap = s->align;
    for (uint32_t f = 0; f < frames; ++f) {
        for (uint32_t c = 0; c < s->channels; ++c) {
            s->fifo[which][((s->wpos[which] % cap) * s->channels) + c] = src[(size_t)f * s->channels + c];
        }
        s->wpos[which]++;
    }
    s->stored[which] += frames;
    if (s->stored[which] > 0 && s->stored[which] % cap == 0) {
        /* keep at most stored modulo cap; read side consumes aligned chunks */
    }
}

static int rs_process(void* state, const OrpheusProcessContext* ctx) {
    RateSyncState* s = (RateSyncState*)state;
    if (ctx->input_count < 2 || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;

    const OrpheusBuffer* in0 = ctx->inputs[0];
    const OrpheusBuffer* in1 = ctx->inputs[1];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in0 == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t ch = s->channels;
    uint32_t align = s->align;
    if (align == 0) align = 1;

    if (in0 != NULL && in0->data != NULL && ctx->frame_count > 0) {
        fifo_append(s, 0, (const float*)in0->data, ctx->frame_count);
    }
    if (in1 != NULL && in1->data != NULL && ctx->frame_count > 0) {
        fifo_append(s, 1, (const float*)in1->data, ctx->frame_count);
    }

    out->frame_count = 0;
    if (s->stored[0] >= align && s->stored[1] >= align) {
        float* out_data = (float*)out->data;
        uint32_t cap = out->frame_capacity < align ? out->frame_capacity : align;
        uint32_t nread = cap;
        for (uint32_t f = 0; f < nread; ++f) {
            for (uint32_t c = 0; c < ch; ++c) {
                float a = s->fifo[0][((s->rpos[0] % align) * ch) + c];
                float b = s->fifo[1][((s->rpos[1] % align) * ch) + c];
                out_data[(size_t)f * ch + c] = a + b;
            }
            s->rpos[0]++;
            s->rpos[1]++;
        }
        s->stored[0] -= nread;
        s->stored[1] -= nread;
        out->frame_count = nread;
    }
    return ORPHEUS_OK;
}

static int rs_get_param(void* state, const char* id, OrpheusValue* v) {
    RateSyncState* s = (RateSyncState*)state;
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    if (strcmp(id, "buffer_length") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->align; return ORPHEUS_OK; }
    if (strcmp(id, "mode") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->mode; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int rs_set_param(void* state, const char* id, const OrpheusValue* v) {
    RateSyncState* s = (RateSyncState*)state;
    /* signature params are restart-required; runtime issues restart so skip live */
    (void)s; (void)id; (void)v;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int rs_register_slots(void* state, const OrpheusRegistry* reg) {
    RateSyncState* s = (RateSyncState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels",
                     "\u901a\u9053\u6570", ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, buffer_length, ORPHEUS_SLOT_SETTING, "buffer_length",
                     "\u7f13\u51b2\u957f\u5ea6", ORPHEUS_VALUE_INT, .min_i32=0, .max_i32=RATE_SYNC_MAX_BUFFER,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface rs_iface = {
    rs_get_descriptor, rs_create, rs_destroy, rs_prepare, rs_reset, rs_process,
    rs_set_param, rs_get_param, NULL, rs_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &rs_iface; }
