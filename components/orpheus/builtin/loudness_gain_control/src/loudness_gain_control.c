#include "orpheus_loudness_gain_control.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LGC_FLOOR_DB (-120.0f)
#define LGC_EPSILON (1.0e-12f)

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] == NULL || strcmp(config->param_ids[i], id) != 0) continue;
        if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {
            return config->param_values[i].value.f32;
        }
        if (config->param_values[i].type == ORPHEUS_VALUE_INT) {
            return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static float clampf(float value, float lower, float upper) {
    if (value < lower) return lower;
    if (value > upper) return upper;
    return value;
}

static float energy_to_db(float energy) {
    if (energy <= LGC_EPSILON) return LGC_FLOOR_DB;
    return 10.0f * log10f(energy);
}

static float block_coeff(float ms, uint32_t frames, uint32_t sample_rate) {
    if (ms <= 0.0f || frames == 0 || sample_rate == 0) return 1.0f;
    return 1.0f - expf(-(float)frames / (ms * 0.001f * (float)sample_rate));
}

static const OrpheusParameter parameters[] = {
    { .id="level", .name="输入线性 RMS", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.0f},
      .min_f32=0.0f, .max_f32=16.0f, .update_policy=ORPHEUS_UPDATE_IMMEDIATE,
      .readback=true },
    { .id="target_db", .name="目标 RMS 电平", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-20.0f},
      .min_f32=-36.0f, .max_f32=-6.0f, .unit="dBFS",
      .update_policy=ORPHEUS_UPDATE_SMOOTHED, .readback=true, .persistent=true },
    { .id="min_gain_db", .name="最大衰减", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-12.0f},
      .min_f32=-36.0f, .max_f32=0.0f, .unit="dB",
      .update_policy=ORPHEUS_UPDATE_SMOOTHED, .readback=true, .persistent=true },
    { .id="max_gain_db", .name="最大提升", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=12.0f},
      .min_f32=0.0f, .max_f32=24.0f, .unit="dB",
      .update_policy=ORPHEUS_UPDATE_SMOOTHED, .readback=true, .persistent=true },
    { .id="gate_db", .name="静音门", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-55.0f},
      .min_f32=-96.0f, .max_f32=-24.0f, .unit="dBFS",
      .update_policy=ORPHEUS_UPDATE_SMOOTHED, .readback=true, .persistent=true },
    { .id="detector_attack_ms", .name="检测启动时间", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=80.0f},
      .min_f32=5.0f, .max_f32=1000.0f, .unit="ms",
      .update_policy=ORPHEUS_UPDATE_SMOOTHED, .readback=true, .persistent=true },
    { .id="detector_release_ms", .name="检测释放时间", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=600.0f},
      .min_f32=20.0f, .max_f32=5000.0f, .unit="ms",
      .update_policy=ORPHEUS_UPDATE_SMOOTHED, .readback=true, .persistent=true },
    { .id="gain_attack_ms", .name="压低响应时间", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=80.0f},
      .min_f32=5.0f, .max_f32=1000.0f, .unit="ms",
      .update_policy=ORPHEUS_UPDATE_SMOOTHED, .readback=true, .persistent=true },
    { .id="gain_release_ms", .name="抬升响应时间", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1200.0f},
      .min_f32=50.0f, .max_f32=10000.0f, .unit="ms",
      .update_policy=ORPHEUS_UPDATE_SMOOTHED, .readback=true, .persistent=true },
    { .id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2},
      .min_i32=1, .max_i32=32, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback=true, .persistent=true, .affects_signature=true },
    { .id="input_db", .name="平滑输入电平", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=LGC_FLOOR_DB},
      .unit="dBFS", .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .readback=true },
    { .id="gain_db", .name="当前目标增益", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.0f},
      .unit="dB", .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .readback=true },
    { .id="output_db", .name="估算输出电平", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=LGC_FLOOR_DB},
      .unit="dBFS", .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .readback=true }
};

static const OrpheusPort ports[] = {
    { .id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
      .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0,
      .is_variable=true, .channels_param="channels" },
    { .id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO,
      .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0,
      .is_variable=true, .channels_param="channels" }
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.loudness_gain_control", .version="1.0.0",
    .abi_version=ORPHEUS_ABI_VERSION, .ports=ports, .port_count=2,
    .params=parameters, .param_count=13, .state_size=sizeof(LoudnessGainControlState),
    .scratch_size=0, .alignment=8, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=true
};

static const OrpheusComponentDescriptor* get_descriptor(void) {
    return &descriptor;
}

static int create(void** state, const OrpheusConfig* config) {
    if (state == NULL) return ORPHEUS_ERR_INVALID_ARG;
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(LoudnessGainControlState));
    return *state != NULL ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int destroy(void* state) {
    (void)state;
    return ORPHEUS_OK;
}

static int prepare(void* state, const OrpheusConfig* config) {
    LoudnessGainControlState* s = (LoudnessGainControlState*)state;
    if (s == NULL || config == NULL) return ORPHEUS_ERR_INVALID_ARG;
    s->level = read_float(config, "level", 0.0f);
    s->target_db = read_float(config, "target_db", -20.0f);
    s->min_gain_db = read_float(config, "min_gain_db", -12.0f);
    s->max_gain_db = read_float(config, "max_gain_db", 12.0f);
    s->gate_db = read_float(config, "gate_db", -55.0f);
    s->detector_attack_ms = read_float(config, "detector_attack_ms", 80.0f);
    s->detector_release_ms = read_float(config, "detector_release_ms", 600.0f);
    s->gain_attack_ms = read_float(config, "gain_attack_ms", 80.0f);
    s->gain_release_ms = read_float(config, "gain_release_ms", 1200.0f);
    s->channels = config->channels > 0 ? config->channels : 2;
    if (s->channels > 32) s->channels = 32;
    s->detector_energy = 0.0f;
    s->gain_db = 0.0f;
    s->input_db = LGC_FLOOR_DB;
    s->output_db = LGC_FLOOR_DB;
    return ORPHEUS_OK;
}

static int reset(void* state) {
    LoudnessGainControlState* s = (LoudnessGainControlState*)state;
    if (s == NULL) return ORPHEUS_ERR_INVALID_ARG;
    s->detector_energy = 0.0f;
    s->gain_db = 0.0f;
    s->input_db = LGC_FLOOR_DB;
    s->output_db = LGC_FLOOR_DB;
    return ORPHEUS_OK;
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    LoudnessGainControlState* s = (LoudnessGainControlState*)state;
    if (s == NULL || ctx == NULL || ctx->inputs == NULL || ctx->outputs == NULL) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    const OrpheusBuffer* input = ctx->inputs[0];
    OrpheusBuffer* output = ctx->outputs[0];
    if (input == NULL || output == NULL || input->data == NULL || output->data == NULL) {
        return ORPHEUS_ERR_INVALID_ARG;
    }

    const float level = s->level >= 0.0f ? s->level : 0.0f;
    const float raw_energy = level * level;
    const float raw_db = energy_to_db(raw_energy);
    if (s->detector_energy <= LGC_EPSILON && raw_energy > LGC_EPSILON) {
        s->detector_energy = raw_energy;
    } else {
        const float detector_ms = raw_energy > s->detector_energy
            ? s->detector_attack_ms : s->detector_release_ms;
        const float coefficient = block_coeff(
            detector_ms, ctx->frame_count, ctx->sample_rate);
        s->detector_energy += coefficient * (raw_energy - s->detector_energy);
    }
    s->input_db = energy_to_db(s->detector_energy);

    float desired_gain_db = s->gain_db;
    if (raw_db >= s->gate_db) {
        const float lower = s->min_gain_db < s->max_gain_db
            ? s->min_gain_db : s->max_gain_db;
        const float upper = s->min_gain_db < s->max_gain_db
            ? s->max_gain_db : s->min_gain_db;
        desired_gain_db = clampf(s->target_db - s->input_db, lower, upper);
    }
    const float gain_ms = desired_gain_db < s->gain_db
        ? s->gain_attack_ms : s->gain_release_ms;
    const float gain_coefficient = block_coeff(
        gain_ms, ctx->frame_count, ctx->sample_rate);
    s->gain_db += gain_coefficient * (desired_gain_db - s->gain_db);
    s->output_db = s->input_db > LGC_FLOOR_DB
        ? s->input_db + s->gain_db : LGC_FLOOR_DB;

    const size_t count = (size_t)ctx->frame_count * s->channels;
    if (output != input) memcpy(output->data, input->data, count * sizeof(float));
    output->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}

static int set_parameter(void* state, const char* id, const OrpheusValue* value) {
    LoudnessGainControlState* s = (LoudnessGainControlState*)state;
    if (s == NULL || id == NULL || value == NULL || value->type != ORPHEUS_VALUE_FLOAT) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    if (strcmp(id, "level") == 0) s->level = value->value.f32;
    else if (strcmp(id, "target_db") == 0) s->target_db = value->value.f32;
    else if (strcmp(id, "min_gain_db") == 0) s->min_gain_db = value->value.f32;
    else if (strcmp(id, "max_gain_db") == 0) s->max_gain_db = value->value.f32;
    else if (strcmp(id, "gate_db") == 0) s->gate_db = value->value.f32;
    else if (strcmp(id, "detector_attack_ms") == 0) s->detector_attack_ms = value->value.f32;
    else if (strcmp(id, "detector_release_ms") == 0) s->detector_release_ms = value->value.f32;
    else if (strcmp(id, "gain_attack_ms") == 0) s->gain_attack_ms = value->value.f32;
    else if (strcmp(id, "gain_release_ms") == 0) s->gain_release_ms = value->value.f32;
    else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}

static int get_parameter(void* state, const char* id, OrpheusValue* value) {
    LoudnessGainControlState* s = (LoudnessGainControlState*)state;
    if (s == NULL || id == NULL || value == NULL) return ORPHEUS_ERR_INVALID_ARG;
    value->type = ORPHEUS_VALUE_FLOAT;
    if (strcmp(id, "level") == 0) value->value.f32 = s->level;
    else if (strcmp(id, "target_db") == 0) value->value.f32 = s->target_db;
    else if (strcmp(id, "min_gain_db") == 0) value->value.f32 = s->min_gain_db;
    else if (strcmp(id, "max_gain_db") == 0) value->value.f32 = s->max_gain_db;
    else if (strcmp(id, "gate_db") == 0) value->value.f32 = s->gate_db;
    else if (strcmp(id, "detector_attack_ms") == 0) value->value.f32 = s->detector_attack_ms;
    else if (strcmp(id, "detector_release_ms") == 0) value->value.f32 = s->detector_release_ms;
    else if (strcmp(id, "gain_attack_ms") == 0) value->value.f32 = s->gain_attack_ms;
    else if (strcmp(id, "gain_release_ms") == 0) value->value.f32 = s->gain_release_ms;
    else if (strcmp(id, "input_db") == 0) value->value.f32 = s->input_db;
    else if (strcmp(id, "gain_db") == 0) value->value.f32 = s->gain_db;
    else if (strcmp(id, "output_db") == 0) value->value.f32 = s->output_db;
    else if (strcmp(id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
    } else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}

static int register_slots(void* state, const OrpheusRegistry* registry) {
    LoudnessGainControlState* s = (LoudnessGainControlState*)state;
    ORPHEUS_REG_SLOT(registry, s, level, ORPHEUS_SLOT_SETTING,
        "level", "输入线性 RMS", ORPHEUS_VALUE_FLOAT,
        .min_f32=0.0f, .max_f32=16.0f,
        .update_policy=ORPHEUS_UPDATE_IMMEDIATE,
        .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(registry, s, target_db, ORPHEUS_SLOT_SETTING,
        "target_db", "目标 RMS 电平", ORPHEUS_VALUE_FLOAT,
        .min_f32=-36.0f, .max_f32=-6.0f, .unit="dBFS",
        .update_policy=ORPHEUS_UPDATE_SMOOTHED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(registry, s, min_gain_db, ORPHEUS_SLOT_SETTING,
        "min_gain_db", "最大衰减", ORPHEUS_VALUE_FLOAT,
        .min_f32=-36.0f, .max_f32=0.0f, .unit="dB",
        .update_policy=ORPHEUS_UPDATE_SMOOTHED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(registry, s, max_gain_db, ORPHEUS_SLOT_SETTING,
        "max_gain_db", "最大提升", ORPHEUS_VALUE_FLOAT,
        .min_f32=0.0f, .max_f32=24.0f, .unit="dB",
        .update_policy=ORPHEUS_UPDATE_SMOOTHED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(registry, s, gate_db, ORPHEUS_SLOT_SETTING,
        "gate_db", "静音门", ORPHEUS_VALUE_FLOAT,
        .min_f32=-96.0f, .max_f32=-24.0f, .unit="dBFS",
        .update_policy=ORPHEUS_UPDATE_SMOOTHED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(registry, s, detector_attack_ms, ORPHEUS_SLOT_SETTING,
        "detector_attack_ms", "检测启动时间", ORPHEUS_VALUE_FLOAT,
        .min_f32=5.0f, .max_f32=1000.0f, .unit="ms",
        .update_policy=ORPHEUS_UPDATE_SMOOTHED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(registry, s, detector_release_ms, ORPHEUS_SLOT_SETTING,
        "detector_release_ms", "检测释放时间", ORPHEUS_VALUE_FLOAT,
        .min_f32=20.0f, .max_f32=5000.0f, .unit="ms",
        .update_policy=ORPHEUS_UPDATE_SMOOTHED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(registry, s, gain_attack_ms, ORPHEUS_SLOT_SETTING,
        "gain_attack_ms", "压低响应时间", ORPHEUS_VALUE_FLOAT,
        .min_f32=5.0f, .max_f32=1000.0f, .unit="ms",
        .update_policy=ORPHEUS_UPDATE_SMOOTHED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(registry, s, gain_release_ms, ORPHEUS_SLOT_SETTING,
        "gain_release_ms", "抬升响应时间", ORPHEUS_VALUE_FLOAT,
        .min_f32=50.0f, .max_f32=10000.0f, .unit="ms",
        .update_policy=ORPHEUS_UPDATE_SMOOTHED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(registry, s, channels, ORPHEUS_SLOT_SETTING,
        "channels", "通道数", ORPHEUS_VALUE_INT,
        .min_i32=1, .max_i32=32,
        .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(registry, s, input_db, ORPHEUS_SLOT_PROBE,
        "input_db", "平滑输入电平", ORPHEUS_VALUE_FLOAT,
        .unit="dBFS", .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry, s, gain_db, ORPHEUS_SLOT_PROBE,
        "gain_db", "当前目标增益", ORPHEUS_VALUE_FLOAT,
        .unit="dB", .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry, s, output_db, ORPHEUS_SLOT_PROBE,
        "output_db", "估算输出电平", ORPHEUS_VALUE_FLOAT,
        .unit="dBFS", .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface interface = {
    get_descriptor, create, destroy, prepare, reset, process,
    set_parameter, get_parameter, NULL, register_slots, NULL
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &interface;
}
