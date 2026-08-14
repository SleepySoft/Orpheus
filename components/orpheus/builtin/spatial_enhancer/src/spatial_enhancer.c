#include "orpheus_spatial_enhancer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

/* Same-frame Mid/Side spatial enhancer (baf DownmixToStereo / SpatialFader style).
   NO time-domain delay -> NO comb filter, NO mid/side time misalignment.
   mid=(L+R)/2, side=(L-R)/2. Processed in the same frame, so the neutral
   setting (width=1, air=0, mono_mix=1) is bit-identical passthrough.
   air = one-pole high-pass on the side signal -> adds "open/clear" upper end. */
static const OrpheusParameter se_params[] = {
    { .id = "width", .name = "Width", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .min_f32 = 0.0f, .max_f32 = 4.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "air", .name = "Air", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = 0.0f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "mono_mix", .name = "Mono Mix", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .min_f32 = 0.0f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "air_fc", .name = "Air Cutoff", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 6000.0f },
      .min_f32 = 500.0f, .max_f32 = 15000.0f, .unit = "Hz",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 2, .max_i32 = 2, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort se_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor se_descriptor = {
    .id = "orpheus.builtin.spatial_enhancer", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = se_ports, .port_count = 2, .params = se_params, .param_count = 5,
    .state_size = sizeof(SpatialEnhancerState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* se_get_descriptor(void) { return &se_descriptor; }

static int se_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(SpatialEnhancerState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int se_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int se_prepare(void* state, const OrpheusConfig* config) {
    SpatialEnhancerState* s = (SpatialEnhancerState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    float width = read_float(config, "width", 1.0f);
    float air = read_float(config, "air", 0.0f);
    float mono_mix = read_float(config, "mono_mix", 1.0f);
    float air_fc = read_float(config, "air_fc", 6000.0f);
    if (width < 0.0f) width = 0.0f;
    if (width > 4.0f) width = 4.0f;
    if (air < 0.0f) air = 0.0f;
    if (air > 1.0f) air = 1.0f;
    if (mono_mix < 0.0f) mono_mix = 0.0f;
    if (mono_mix > 1.0f) mono_mix = 1.0f;
    s->air_fc = air_fc;
    s->width = width; s->width_smoothed = width;
    s->air = air; s->air_smoothed = air;
    s->mono_mix = mono_mix; s->mono_mix_smoothed = mono_mix;
    s->mono_coeff = 1.0f;
    if (config->sample_rate > 0) {
        float tau = 0.005f;
        s->mono_coeff = 1.0f - expf(-1.0f / (tau * (float)config->sample_rate));
        if (s->mono_coeff > 1.0f) s->mono_coeff = 1.0f;
        /* unity-gain-at-Nyquist one-pole high-pass: y[n]=b0(x[n]-x[n-1])+alpha*y[n-1]
           with alpha=(1-cos(w))/(1+sin(w)), b0=(1+alpha)/2.
           Passband gain ~1 (not a pure differentiator), so air brightens the
           side signal instead of mostly attenuating it. */
        float fc = air_fc < 500.0f ? 500.0f : air_fc;
        float w = 2.0f * 3.14159265f * fc / (float)config->sample_rate;
        float cw = cosf(w), sw = sinf(w);
        float alpha = (1.0f - cw) / (1.0f + sw);
        s->hp_fb = alpha;
        s->hp_gain = (1.0f + alpha) * 0.5f;
    }
    s->hp_prev_side = 0.0f;
    s->hp_prev_out = 0.0f;
    return ORPHEUS_OK;
}
static int se_reset(void* state) {
    SpatialEnhancerState* s = (SpatialEnhancerState*)state;
    s->width_smoothed = s->width;
    s->air_smoothed = s->air;
    s->mono_mix_smoothed = s->mono_mix;
    s->hp_prev_side = 0.0f;
    s->hp_prev_out = 0.0f;
    return ORPHEUS_OK;
}
static int se_process(void* state, const OrpheusProcessContext* ctx) {
    SpatialEnhancerState* s = (SpatialEnhancerState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in || !out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t n = 0; n < frames; ++n) {
        float L = in_data[n * ch + 0];
        float R = in_data[n * ch + 1];
        float mid = (L + R) * 0.5f;
        float side = (L - R) * 0.5f;
        /* smooth the three knobs */
        s->width_smoothed += s->mono_coeff * (s->width - s->width_smoothed);
        s->air_smoothed += s->mono_coeff * (s->air - s->air_smoothed);
        s->mono_mix_smoothed += s->mono_coeff * (s->mono_mix - s->mono_mix_smoothed);
        float w = s->width_smoothed;
        float a = s->air_smoothed;
        float mm = s->mono_mix_smoothed;
        /* air: one-pole high-pass on side (unity-gain at Nyquist), adds
           upper-end openness by removing low-end side mud */
        float hp = s->hp_gain * (side - s->hp_prev_side) + s->hp_fb * s->hp_prev_out;
        s->hp_prev_side = side;
        s->hp_prev_out = hp;
        /* crossfade between raw side (air=0) and high-passed side (air=1) */
        float side_hp = side * (1.0f - a) + hp * a;
        float side_eff = side_hp * w;
        float side_out = side_eff * mm;
        out_data[n * ch + 0] = mid + side_out;
        out_data[n * ch + 1] = mid - side_out;
        for (uint32_t c = 2; c < ch; ++c) {
            out_data[n * ch + c] = in_data[n * ch + c];
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int se_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    SpatialEnhancerState* s = (SpatialEnhancerState*)state;
    if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
    float v = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
    if (strcmp(param_id, "width") == 0) { if(v<0)v=0; if(v>4)v=4; s->width=v; return ORPHEUS_OK; }
    if (strcmp(param_id, "air") == 0) { if(v<0)v=0; if(v>1)v=1; s->air=v; return ORPHEUS_OK; }
    if (strcmp(param_id, "mono_mix") == 0) { if(v<0)v=0; if(v>1)v=1; s->mono_mix=v; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int se_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    SpatialEnhancerState* s = (SpatialEnhancerState*)state;
    if (strcmp(param_id, "width") == 0) { value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->width; return ORPHEUS_OK; }
    if (strcmp(param_id, "air") == 0) { value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->air; return ORPHEUS_OK; }
    if (strcmp(param_id, "mono_mix") == 0) { value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->mono_mix; return ORPHEUS_OK; }
    if (strcmp(param_id, "channels") == 0) { value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int se_register_slots(void* state, const OrpheusRegistry* reg) {
    SpatialEnhancerState* s = (SpatialEnhancerState*)state;
    ORPHEUS_REG_SLOT(reg, s, width, ORPHEUS_SLOT_SETTING, "width", "展宽",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=4.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, air, ORPHEUS_SLOT_SETTING, "air", "空气感",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, mono_mix, ORPHEUS_SLOT_SETTING, "mono_mix", "单声混合",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, air_fc, ORPHEUS_SLOT_SETTING, "air_fc", "空气感截止",
                     ORPHEUS_VALUE_FLOAT, .min_f32=500.0f, .max_f32=15000.0f, .unit="Hz",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=2, .max_i32=2,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface se_interface = {
    .get_descriptor = se_get_descriptor, .create = se_create, .destroy = se_destroy,
    .prepare = se_prepare, .reset = se_reset, .process = se_process,
    .set_parameter = se_set_parameter, .get_parameter = se_get_parameter, .get_state_value = NULL,
    .register_slots = se_register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &se_interface; }
