#include "orpheus_wav_out.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define ORPHEUS_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define ORPHEUS_MKDIR(p) mkdir(p, 0755)
#endif

#define WAV_OUT_MAX_SAMPLES (1024 * 1024 * 16)

static const OrpheusParameter wav_out_params[] = {
    {
        .id = "file_path",
        .name = "File Path",
        .type = ORPHEUS_VALUE_STRING,
        .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "" },
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = false
    },
    {
        .id = "channels",
        .name = "Channels",
        .type = ORPHEUS_VALUE_INT,
        .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
        .min_i32 = 1,
        .max_i32 = 32,
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = true
    },
    {
        .id = "sample_rate",
        .name = "Sample Rate",
        .type = ORPHEUS_VALUE_INT,
        .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 48000 },
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = false
    }
};

static const OrpheusPort wav_out_ports[] = {
    {
        .id = "in",
        .direction = ORPHEUS_PORT_INPUT,
        .type = ORPHEUS_PORT_AUDIO,
        .sample_format = ORPHEUS_FORMAT_F32,
        .channels = 0,
        .sample_rate = 0,
        .block_size = 0,
        .is_variable = true,
        .channels_param = "channels"
    }
};

static const OrpheusComponentDescriptor wav_out_descriptor = {
    .id = "orpheus.builtin.wav_out",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = wav_out_ports,
    .port_count = 1,
    .params = wav_out_params,
    .param_count = 3,
    .state_size = sizeof(WavOutState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = false,
    .supports_inplace = false
};

static void write_u16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void write_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static int wav_out_write_file(WavOutState* s) {
    /* 确保输出目录存在（离线会话/直接运行不会预建 outputs/，fopen 静默失败） */
    const char* slash = strrchr(s->file_path, '/');
    const char* bslash = strrchr(s->file_path, '\\');
    const char* sep = bslash && (!slash || bslash > slash) ? bslash : slash;
    if (sep != NULL && sep != s->file_path) {
        char dirbuf[512];
        size_t len = (size_t)(sep - s->file_path);
        if (len < sizeof(dirbuf)) {
            memcpy(dirbuf, s->file_path, len);
            dirbuf[len] = '\0';
            ORPHEUS_MKDIR(dirbuf);  /* 已存在则忽略 EEXIST */
        }
    }
    FILE* f = fopen(s->file_path, "wb");
    if (!f) return ORPHEUS_ERR_NOT_FOUND;

    uint32_t data_size = s->count * 2;
    uint32_t byte_rate = s->sample_rate * s->channels * 2;
    uint16_t block_align = (uint16_t)(s->channels * 2);

    uint8_t header[44];
    memcpy(header, "RIFF", 4);
    write_u32(header + 4, 36 + data_size);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    write_u32(header + 16, 16);
    write_u16(header + 20, 1); /* PCM */
    write_u16(header + 22, (uint16_t)s->channels);
    write_u32(header + 24, s->sample_rate);
    write_u32(header + 28, byte_rate);
    write_u16(header + 32, block_align);
    write_u16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    write_u32(header + 40, data_size);

    fwrite(header, 1, 44, f);

    for (uint32_t i = 0; i < s->count; ++i) {
        float v = s->samples[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        int16_t vi = (int16_t)(v * 32767.0f);
        uint8_t buf[2];
        write_u16(buf, (uint16_t)vi);
        fwrite(buf, 1, 2, f);
    }

    fclose(f);
    return ORPHEUS_OK;
}

static const OrpheusComponentDescriptor* wav_out_get_descriptor(void) {
    return &wav_out_descriptor;
}

static int wav_out_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(WavOutState));
    if (*state == NULL) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return ORPHEUS_OK;
}

static int wav_out_destroy(void* state) {
    WavOutState* s = (WavOutState*)state;
    if (s->samples && strlen(s->file_path) > 0) {
        wav_out_write_file(s);
    }
    if (s->samples) free(s->samples);
    /* v2：状态块本身由 Runtime 统一管理（落盘逻辑保留） */
    return ORPHEUS_OK;
}

static int wav_out_prepare(void* state, const OrpheusConfig* config) {
    WavOutState* s = (WavOutState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->sample_rate = config->sample_rate > 0 ? config->sample_rate : 48000;
    s->count = 0;
    s->capacity = WAV_OUT_MAX_SAMPLES;
    s->samples = (float*)malloc(s->capacity * sizeof(float));
    if (!s->samples) return ORPHEUS_ERR_OUT_OF_MEMORY;

    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "file_path") == 0) {
            const char* path = config->param_values[i].value.str;
            if (path) {
                strncpy(s->file_path, path, sizeof(s->file_path) - 1);
                s->file_path[sizeof(s->file_path) - 1] = '\0';
            }
        }
        if (config->param_ids[i] && strcmp(config->param_ids[i], "sample_rate") == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) {
                s->sample_rate = (uint32_t)config->param_values[i].value.i32;
            }
        }
    }

    return ORPHEUS_OK;
}

static int wav_out_reset(void* state) {
    WavOutState* s = (WavOutState*)state;
    s->count = 0;
    return ORPHEUS_OK;
}

static int wav_out_process(void* state, const OrpheusProcessContext* ctx) {
    WavOutState* s = (WavOutState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    const float* in_data = (const float*)in->data;
    uint32_t samples = ctx->frame_count * s->channels;

    if (s->count + samples > s->capacity) {
        samples = s->capacity - s->count;
    }

    for (uint32_t i = 0; i < samples; ++i) {
        s->samples[s->count + i] = in_data[i];
    }
    s->count += samples;

    return ORPHEUS_OK;
}

static int wav_out_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int wav_out_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    WavOutState* s = (WavOutState*)state;
    if (strcmp(param_id, "file_path") == 0) {
        value->type = ORPHEUS_VALUE_STRING;
        value->value.str = s->file_path;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int wav_out_register_slots(void* state, const OrpheusRegistry* reg) {
    WavOutState* s = (WavOutState*)state;
    ORPHEUS_REG_SLOT(reg, s, file_path, ORPHEUS_SLOT_SETTING, "file_path", "文件路径",
                     ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, sample_rate, ORPHEUS_SLOT_SETTING, "sample_rate", "采样率",
                     ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface wav_out_interface = {
    .get_descriptor = wav_out_get_descriptor,
    .create = wav_out_create,
    .destroy = wav_out_destroy,
    .prepare = wav_out_prepare,
    .reset = wav_out_reset,
    .process = wav_out_process,
    .set_parameter = wav_out_set_parameter,
    .get_parameter = wav_out_get_parameter,
    .get_state_value = NULL,
    .register_slots = wav_out_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &wav_out_interface;
}
