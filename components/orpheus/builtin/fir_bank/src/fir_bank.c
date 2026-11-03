#include "orpheus_fir_bank.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2},
     .min_i32=1, .max_i32=FIR_BANK_MAX_CHANNELS,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true},
    {.id="num_taps", .name="每通道系数跨度", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1},
     .min_i32=1, .max_i32=FIR_BANK_MAX_TAPS,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true},
    {.id="taps_per_channel", .name="每通道有效抽头数", .type=ORPHEUS_VALUE_STRING,
     .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED},
    {.id="coefs_mode", .name="系数模式", .type=ORPHEUS_VALUE_STRING,
     .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="shared"},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .affects_signature=true},
    {.id="coefs", .name="FIR初始系数", .type=ORPHEUS_VALUE_STRING,
     .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED},
    {.id="processing_enabled", .name="处理使能", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1},
     .min_i32=0, .max_i32=1, .update_policy=ORPHEUS_UPDATE_IMMEDIATE,
     .readback=true, .persistent=true}
};

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"}
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.fir_bank", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports, .port_count=2, .params=params, .param_count=6,
    .state_size=sizeof(FirBankState), .alignment=8,
    .realtime_safe=true, .supports_inplace=true
};

static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static int create(void** state, const OrpheusConfig* config) {
    if (config && config->state_block) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(FirBankState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int destroy(void* state) {
    FirBankState* s = (FirBankState*)state;
    free(s->history);
    s->history = NULL;
    return ORPHEUS_OK;
}

static const char* skip_space(const char* p) {
    while (isspace((unsigned char)*p)) ++p;
    return p;
}

static int delimiter(const char** cursor, uint32_t index, uint32_t count) {
    const char* p = skip_space(*cursor);
    if (index+1 < count) {
        if (*p != ',') return ORPHEUS_ERR_INVALID_ARG;
        *cursor = skip_space(p+1);
    } else {
        if (*p) return ORPHEUS_ERR_INVALID_ARG;
        *cursor = p;
    }
    return ORPHEUS_OK;
}

static int prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config) return ORPHEUS_ERR_INVALID_ARG;
    FirBankState* s = (FirBankState*)state;
    uint32_t channels = config->channels ? config->channels : 2;
    uint32_t taps = 1, mode = 0;
    int32_t enabled = 1;
    const char* coefficients = "";
    const char* lengths = "";
    if (channels > FIR_BANK_MAX_CHANNELS) return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        const char* id = config->param_ids[i];
        const OrpheusValue* value = &config->param_values[i];
        if (!id) continue;
        if (strcmp(id, "num_taps") == 0) {
            if (value->type != ORPHEUS_VALUE_INT || value->value.i32 < 1 ||
                value->value.i32 > FIR_BANK_MAX_TAPS) return ORPHEUS_ERR_INVALID_ARG;
            taps = (uint32_t)value->value.i32;
        } else if (strcmp(id, "processing_enabled") == 0) {
            if (value->type != ORPHEUS_VALUE_INT || value->value.i32 < 0 ||
                value->value.i32 > 1) return ORPHEUS_ERR_INVALID_ARG;
            enabled = value->value.i32;
        } else if (strcmp(id, "coefs_mode") == 0 || strcmp(id, "coefs") == 0 ||
                   strcmp(id, "taps_per_channel") == 0) {
            if (value->type != ORPHEUS_VALUE_STRING || !value->value.str)
                return ORPHEUS_ERR_INVALID_ARG;
            if (strcmp(id, "coefs") == 0) coefficients = value->value.str;
            else if (strcmp(id, "taps_per_channel") == 0) lengths = value->value.str;
            else if (strcmp(value->value.str, "per_channel") == 0) mode = 1;
            else if (strcmp(value->value.str, "shared") == 0) mode = 0;
            else return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    uint32_t count = (mode ? channels : 1)*taps;
    if (count > FIR_BANK_MAX_COEFS) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t parsed_lengths[FIR_BANK_MAX_CHANNELS];
    const char* p = skip_space(lengths);
    for (uint32_t c = 0; c < channels; ++c) {
        parsed_lengths[c] = taps;
        if (*lengths) {
            char* end = NULL;
            if (*p == '-') return ORPHEUS_ERR_INVALID_ARG;
            unsigned long value = strtoul(p, &end, 10);
            if (end == p || value > taps) return ORPHEUS_ERR_INVALID_ARG;
            parsed_lengths[c] = (uint32_t)value;
            p = end;
            if (delimiter(&p, c, channels) != ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    float* next_coefs = (float*)calloc(count, sizeof(float));
    if (!next_coefs) return ORPHEUS_ERR_OUT_OF_MEMORY;
    p = skip_space(coefficients);
    if (!*p) {
        for (uint32_t c = 0; c < (mode ? channels : 1); ++c) next_coefs[c*taps] = 1.0f;
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            char* end = NULL;
            next_coefs[i] = strtof(p, &end);
            if (end == p || !isfinite(next_coefs[i])) {
                free(next_coefs);
                return ORPHEUS_ERR_INVALID_ARG;
            }
            p = end;
            if (delimiter(&p, i, count) != ORPHEUS_OK) {
                free(next_coefs);
                return ORPHEUS_ERR_INVALID_ARG;
            }
        }
    }
    float* history = (float*)calloc((size_t)channels*taps, sizeof(float));
    if (!history) {
        free(next_coefs);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    free(s->history);
    s->history = history;
    s->channels = channels;
    s->num_taps = taps;
    s->coefs_mode = mode;
    s->processing_enabled = enabled;
    memcpy(s->taps_per_channel, parsed_lengths, channels*sizeof(uint32_t));
    memset(s->positions, 0, sizeof(s->positions));
    memset(s->coefs, 0, sizeof(s->coefs));
    memcpy(s->coefs, next_coefs, count*sizeof(float));
    free(next_coefs);
    return ORPHEUS_OK;
}

static int reset(void* state) {
    FirBankState* s = (FirBankState*)state;
    if (s->history) memset(s->history, 0, (size_t)s->channels*s->num_taps*sizeof(float));
    memset(s->positions, 0, sizeof(s->positions));
    return ORPHEUS_OK;
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    FirBankState* s = (FirBankState*)state;
    if (!ctx->outputs[0] || !s->history) return ORPHEUS_ERR_INVALID_ARG;
    OrpheusBuffer* out = ctx->outputs[0];
    float* output = (float*)out->data;
    if (!s->processing_enabled) {
        memset(output, 0, (size_t)ctx->frame_count*s->channels*sizeof(float));
    } else {
        if (!ctx->inputs[0]) return ORPHEUS_ERR_INVALID_ARG;
        const float* input = (const float*)ctx->inputs[0]->data;
        for (uint32_t f = 0; f < ctx->frame_count; ++f) {
            for (uint32_t c = 0; c < s->channels; ++c) {
                uint32_t length = s->taps_per_channel[c];
                if (!length) {
                    output[(size_t)f*s->channels+c] = 0.0f;
                    continue;
                }
                float* history = s->history+(size_t)c*s->num_taps;
                const float* coefs = s->coefs+(s->coefs_mode ? c*s->num_taps : 0);
                uint32_t position = s->positions[c], index = position;
                history[position] = input[(size_t)f*s->channels+c];
                float sum = 0.0f;
                for (uint32_t k = 0; k < length; ++k) {
                    sum += coefs[k]*history[index];
                    index = index ? index-1 : length-1;
                }
                output[(size_t)f*s->channels+c] = sum;
                s->positions[c] = position+1 == length ? 0 : position+1;
            }
        }
    }
    out->frame_count = ctx->frame_count;
    out->interleaved = true;
    return ORPHEUS_OK;
}

static int set_parameter(void* state, const char* id, const OrpheusValue* value) {
    if (!state || !id || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(id, "processing_enabled") != 0) return ORPHEUS_ERR_UNSUPPORTED;
    if (value->type != ORPHEUS_VALUE_INT || value->value.i32 < 0 || value->value.i32 > 1)
        return ORPHEUS_ERR_INVALID_ARG;
    ((FirBankState*)state)->processing_enabled = value->value.i32;
    return ORPHEUS_OK;
}

static int get_parameter(void* state, const char* id, OrpheusValue* value) {
    if (!state || !id || !value) return ORPHEUS_ERR_INVALID_ARG;
    FirBankState* s = (FirBankState*)state;
    value->type = ORPHEUS_VALUE_INT;
    if (strcmp(id, "channels") == 0) value->value.i32 = (int32_t)s->channels;
    else if (strcmp(id, "num_taps") == 0) value->value.i32 = (int32_t)s->num_taps;
    else if (strcmp(id, "processing_enabled") == 0) value->value.i32 = s->processing_enabled;
    else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}

static int register_slots(void* state, const OrpheusRegistry* reg) {
    FirBankState* s = (FirBankState*)state;
    OrpheusSlotInfo slot = {
        .kind=ORPHEUS_SLOT_BULK, .key=s->coefs_mode ? "coefs_per_channel" : "coefs",
        .name="FIR系数", .type=ORPHEUS_VALUE_FLOAT,
        .offset=offsetof(FirBankState, coefs), .size=sizeof(float),
        .count=(s->coefs_mode ? s->channels : 1)*s->num_taps,
        .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED
    };
    reg->add(reg->ctx, &slot);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                    ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=FIR_BANK_MAX_CHANNELS,
                    .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, num_taps, ORPHEUS_SLOT_SETTING, "num_taps", "每通道系数跨度",
                    ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=FIR_BANK_MAX_TAPS,
                    .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, processing_enabled, ORPHEUS_SLOT_SETTING, "processing_enabled", "处理使能",
                    ORPHEUS_VALUE_INT, .min_i32=0, .max_i32=1,
                    .update_policy=ORPHEUS_UPDATE_IMMEDIATE,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface interface = {
    .get_descriptor=get_descriptor, .create=create, .destroy=destroy,
    .prepare=prepare, .reset=reset, .process=process,
    .set_parameter=set_parameter, .get_parameter=get_parameter, .register_slots=register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
