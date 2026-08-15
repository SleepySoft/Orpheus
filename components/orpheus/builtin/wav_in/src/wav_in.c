#include "orpheus_wav_in.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define WAV_IN_MAX_FRAMES (1024 * 1024 * 16) /* 16M 帧上限（与 mp3_in 口径一致） */

#ifdef _WIN32
/* Windows 窄 fopen 按 ANSI 代码页解释路径，UTF-8 中文文件名打不开，
   这里转成 UTF-16 使用宽字符 API。 */
static wchar_t* utf8_to_wide(const char* utf8) {
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t* w = (wchar_t*)malloc((size_t)len * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, len);
    return w;
}
#endif

static const OrpheusParameter wav_in_params[] = {
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
    }
};

static const OrpheusPort wav_in_ports[] = {
    {
        .id = "out",
        .direction = ORPHEUS_PORT_OUTPUT,
        .type = ORPHEUS_PORT_AUDIO,
        .sample_format = ORPHEUS_FORMAT_F32,
        .channels = 0,
        .sample_rate = 0,
        .block_size = 0,
        .is_variable = true,
        .channels_param = "channels"
    }
};

static const OrpheusComponentDescriptor wav_in_descriptor = {
    .id = "orpheus.builtin.wav_in",
    .version = "1.0.1",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = wav_in_ports,
    .port_count = 1,
    .params = wav_in_params,
    .param_count = 2,
    .state_size = sizeof(WavInState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = false,
    .supports_inplace = false
};

static uint16_t read_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int wav_read_file(const char* path, float** out_samples, uint32_t* out_total_frames, uint32_t channels) {
#ifdef _WIN32
    wchar_t* wpath = utf8_to_wide(path);
    FILE* f = wpath ? _wfopen(wpath, L"rb") : NULL;
    free(wpath);
#else
    FILE* f = fopen(path, "rb");
#endif
    if (!f) return ORPHEUS_ERR_NOT_FOUND;

    uint8_t header[44];
    if (fread(header, 1, 44, f) != 44) {
        fclose(f);
        return ORPHEUS_ERR_INVALID_ARG;
    }

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        fclose(f);
        return ORPHEUS_ERR_INVALID_ARG;
    }

    uint16_t audio_format = read_u16(header + 20);
    uint16_t file_channels = read_u16(header + 22);
    uint32_t sample_rate = read_u32(header + 24);
    uint16_t bits_per_sample = read_u16(header + 34);
    uint32_t data_size = read_u32(header + 40);

    (void)sample_rate;

    uint32_t bytes_per_sample = bits_per_sample / 8;
    uint32_t total_samples = data_size / bytes_per_sample;
    uint32_t total_frames = total_samples / file_channels;

    if (total_frames > WAV_IN_MAX_FRAMES) {
        fclose(f);
        return ORPHEUS_ERR_INVALID_ARG;
    }

    float* samples = (float*)malloc(total_samples * sizeof(float));
    if (!samples) {
        fclose(f);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }

    uint8_t* raw = (uint8_t*)malloc(data_size);
    if (!raw) {
        free(samples);
        fclose(f);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }

    if (fread(raw, 1, data_size, f) != data_size) {
        free(raw);
        free(samples);
        fclose(f);
        return ORPHEUS_ERR_INVALID_ARG;
    }
    fclose(f);

    for (uint32_t i = 0; i < total_samples; ++i) {
        if (audio_format == 1) { /* PCM */
            if (bits_per_sample == 16) {
                int16_t v = (int16_t)read_u16(raw + i * 2);
                samples[i] = v / 32768.0f;
            } else if (bits_per_sample == 32) {
                int32_t v = (int32_t)read_u32(raw + i * 4);
                samples[i] = v / 2147483648.0f;
            } else {
                samples[i] = 0.0f;
            }
        } else if (audio_format == 3 && bits_per_sample == 32) { /* float */
            memcpy(&samples[i], raw + i * 4, 4);
        } else {
            samples[i] = 0.0f;
        }
    }

    free(raw);

    /* 文件通道数与目标不一致时做简单映射：不足则重复第 0 通道，超出则丢弃 */
    if (file_channels != channels) {
        float* remapped = (float*)malloc(total_frames * channels * sizeof(float));
        if (!remapped) {
            free(samples);
            return ORPHEUS_ERR_OUT_OF_MEMORY;
        }
        for (uint32_t frame = 0; frame < total_frames; ++frame) {
            for (uint32_t ch = 0; ch < channels; ++ch) {
                uint32_t src_ch = ch < file_channels ? ch : 0;
                remapped[frame * channels + ch] = samples[frame * file_channels + src_ch];
            }
        }
        free(samples);
        samples = remapped;
    }

    *out_samples = samples;
    *out_total_frames = total_frames;
    return ORPHEUS_OK;
}

static const OrpheusComponentDescriptor* wav_in_get_descriptor(void) {
    return &wav_in_descriptor;
}

static int wav_in_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(WavInState));
    if (*state == NULL) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return ORPHEUS_OK;
}

static int wav_in_destroy(void* state) {
    WavInState* s = (WavInState*)state;
    if (s->samples) free(s->samples);
    /* v2：状态块本身由 Runtime 统一管理 */
    return ORPHEUS_OK;
}

static int wav_in_prepare(void* state, const OrpheusConfig* config) {
    WavInState* s = (WavInState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->position = 0;

    const char* path = NULL;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "file_path") == 0) {
            path = config->param_values[i].value.str;
        }
    }

    if (!path || strlen(path) == 0) {
        s->samples = NULL;
        s->total_frames = 0;
        return ORPHEUS_OK;
    }

    strncpy(s->file_path, path, sizeof(s->file_path) - 1);
    s->file_path[sizeof(s->file_path) - 1] = '\0';

    return wav_read_file(s->file_path, &s->samples, &s->total_frames, s->channels);
}

static int wav_in_reset(void* state) {
    WavInState* s = (WavInState*)state;
    s->position = 0;
    return ORPHEUS_OK;
}

static int wav_in_process(void* state, const OrpheusProcessContext* ctx) {
    WavInState* s = (WavInState*)state;
    OrpheusBuffer* out = ctx->outputs[0];
    float* out_data = (float*)out->data;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;

    for (uint32_t i = 0; i < frames * ch; ++i) {
        out_data[i] = 0.0f;
    }

    uint32_t remaining = s->total_frames > s->position ? s->total_frames - s->position : 0;
    uint32_t to_copy = frames < remaining ? frames : remaining;

    for (uint32_t frame = 0; frame < to_copy; ++frame) {
        for (uint32_t c = 0; c < ch; ++c) {
            out_data[frame * ch + c] = s->samples[(s->position + frame) * ch + c];
        }
    }

    s->position += to_copy;
    out->frame_count = frames;
    out->interleaved = true;
    return ORPHEUS_OK;
}

static int wav_in_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int wav_in_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    WavInState* s = (WavInState*)state;
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

static int wav_in_register_slots(void* state, const OrpheusRegistry* reg) {
    WavInState* s = (WavInState*)state;
    ORPHEUS_REG_SLOT(reg, s, file_path, ORPHEUS_SLOT_SETTING, "file_path", "文件路径",
                     ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface wav_in_interface = {
    .get_descriptor = wav_in_get_descriptor,
    .create = wav_in_create,
    .destroy = wav_in_destroy,
    .prepare = wav_in_prepare,
    .reset = wav_in_reset,
    .process = wav_in_process,
    .set_parameter = wav_in_set_parameter,
    .get_parameter = wav_in_get_parameter,
    .get_state_value = NULL,
    .register_slots = wav_in_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &wav_in_interface;
}
