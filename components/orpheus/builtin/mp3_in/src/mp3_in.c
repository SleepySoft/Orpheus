#include "orpheus_mp3_in.h"

#include "miniaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define MP3_IN_MAX_FRAMES (1024 * 1024 * 16) /* 16M 帧上限（基本版） */
#define MP3_IN_READ_CHUNK 4096

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

static const OrpheusParameter mp3_in_params[] = {
    {
        .id = "file_path",
        .name = "MP3 File",
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
        .id = "total_frames",
        .name = "Total Frames",
        .type = ORPHEUS_VALUE_INT,
        .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
        .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
        .readback = true,
        .persistent = false,
        .affects_signature = false
    }
};

static const OrpheusPort mp3_in_ports[] = {
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

static const OrpheusComponentDescriptor mp3_in_descriptor = {
    .id = "orpheus.builtin.mp3_in",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = mp3_in_ports,
    .port_count = 1,
    .params = mp3_in_params,
    .param_count = 3,
    .state_size = sizeof(Mp3InState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = false,
    .supports_inplace = false
};

static const OrpheusComponentDescriptor* mp3_in_get_descriptor(void) {
    return &mp3_in_descriptor;
}

static int mp3_in_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(Mp3InState));
    if (*state == NULL) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return ORPHEUS_OK;
}

static int mp3_in_destroy(void* state) {
    Mp3InState* s = (Mp3InState*)state;
    if (s->samples) free(s->samples);
    /* v2：状态块本身由 Runtime 统一管理 */
    return ORPHEUS_OK;
}

static int mp3_in_prepare(void* state, const OrpheusConfig* config) {
    Mp3InState* s = (Mp3InState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->position = 0;
    s->total_frames = 0;
    if (s->samples) {
        free(s->samples);
        s->samples = NULL;
    }

    const char* path = NULL;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "file_path") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_STRING) {
            path = config->param_values[i].value.str;
        }
    }
    if (!path || path[0] == '\0') return ORPHEUS_OK;

    strncpy(s->file_path, path, sizeof(s->file_path) - 1);
    s->file_path[sizeof(s->file_path) - 1] = '\0';

    uint32_t sample_rate = config->sample_rate > 0 ? config->sample_rate : 48000;
    ma_decoder_config dec_cfg = ma_decoder_config_init(ma_format_f32, s->channels, sample_rate);
    ma_decoder decoder;
#ifdef _WIN32
    wchar_t* wpath = utf8_to_wide(s->file_path);
    if (!wpath) return ORPHEUS_ERR_OUT_OF_MEMORY;
    ma_result mr = ma_decoder_init_file_w(wpath, &dec_cfg, &decoder);
    free(wpath);
#else
    ma_result mr = ma_decoder_init_file(s->file_path, &dec_cfg, &decoder);
#endif
    if (mr != MA_SUCCESS) {
        return ORPHEUS_ERR_NOT_FOUND;
    }

    uint32_t cap = MP3_IN_MAX_FRAMES;
    float* samples = (float*)malloc((size_t)cap * s->channels * sizeof(float));
    if (!samples) {
        ma_decoder_uninit(&decoder);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }

    uint32_t total = 0;
    for (;;) {
        uint32_t want = MP3_IN_READ_CHUNK;
        if (total + want > cap) want = cap - total;
        if (want == 0) break;
        ma_uint64 got = 0;
        ma_result r = ma_decoder_read_pcm_frames(
            &decoder, samples + (size_t)total * s->channels, want, &got);
        total += (uint32_t)got;
        if (r != MA_SUCCESS || got < want) break;
    }
    ma_decoder_uninit(&decoder);

    s->samples = samples;
    s->total_frames = total;
    return ORPHEUS_OK;
}

static int mp3_in_reset(void* state) {
    Mp3InState* s = (Mp3InState*)state;
    s->position = 0;
    return ORPHEUS_OK;
}

static int mp3_in_process(void* state, const OrpheusProcessContext* ctx) {
    Mp3InState* s = (Mp3InState*)state;
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

static int mp3_in_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int mp3_in_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    Mp3InState* s = (Mp3InState*)state;
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
    if (strcmp(param_id, "total_frames") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->total_frames;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int mp3_in_register_slots(void* state, const OrpheusRegistry* reg) {
    Mp3InState* s = (Mp3InState*)state;
    ORPHEUS_REG_SLOT(reg, s, file_path, ORPHEUS_SLOT_SETTING, "file_path", "MP3 文件",
                     ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, total_frames, ORPHEUS_SLOT_PROBE, "total_frames", "总帧数",
                     ORPHEUS_VALUE_INT, .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface mp3_in_interface = {
    .get_descriptor = mp3_in_get_descriptor,
    .create = mp3_in_create,
    .destroy = mp3_in_destroy,
    .prepare = mp3_in_prepare,
    .reset = mp3_in_reset,
    .process = mp3_in_process,
    .set_parameter = mp3_in_set_parameter,
    .get_parameter = mp3_in_get_parameter,
    .get_state_value = NULL,
    .register_slots = mp3_in_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &mp3_in_interface;
}
