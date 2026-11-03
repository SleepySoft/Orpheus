#include "orpheus_fade.h"
#include "orpheus_gain_array.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979f

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static void fade_targets(float fade, float* tf, float* tb) {
    if (fade >= 0.0f) { *tf = 1.0f; *tb = 1.0f - fade; }
    else              { *tf = 1.0f + fade; *tb = 1.0f; }
}

#define FADE_ARRAY_PARAMETER(key,label) \
    {.id=key,.name=label,.type=ORPHEUS_VALUE_STRING, \
     .default_value={.type=ORPHEUS_VALUE_STRING,.value.str=""}, \
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true}
static const OrpheusParameter fade_params[] = {
    { .id = "fade", .name = "Fade", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -1.0f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "crossover_hz", .name = "Crossover", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 400.0f },
      .min_f32 = 100.0f, .max_f32 = 2000.0f, .unit = "Hz", .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 4 },
      .min_i32 = 2, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "front_channels", .name = "Front Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 31, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "ramp_ms", .name = "Ramp Time", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 50.0f },
      .min_f32 = 0.0f, .max_f32 = 1000.0f, .unit = "ms", .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    {.id="execution_mode",.name="执行模式（0内部分频，1外部分频显式包络）",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},.min_i32=0,.max_i32=1,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=true},
    FADE_ARRAY_PARAMETER("chan_map","通道到前后组映射"),
    FADE_ARRAY_PARAMETER("current_gains","高频前后/低频前后当前增益"),
    FADE_ARRAY_PARAMETER("target_gains","高频前后/低频前后目标增益"),
    FADE_ARRAY_PARAMETER("coefficients","高频前后/低频前后幂次乘数"),
    FADE_ARRAY_PARAMETER("remaining_blocks","四组剩余块数（仅前两组驱动）"),
    {.id="freeze",.name="冻结显式包络",.type=ORPHEUS_VALUE_BOOL,
     .default_value={.type=ORPHEUS_VALUE_BOOL,.value.b=false},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true}
};
#undef FADE_ARRAY_PARAMETER

static const OrpheusPort fade_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "lpf", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor fade_descriptor = {
    .id = "orpheus.builtin.fade", .version = "1.1.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = fade_ports, .port_count = 3, .params = fade_params,
    .param_count = sizeof(fade_params)/sizeof(fade_params[0]),
    .state_size = sizeof(FadeState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* fade_get_descriptor(void) { return &fade_descriptor; }

static int fade_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(FadeState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int fade_destroy(void* state) { (void)state; return ORPHEUS_OK; } /* v2：内存由 Runtime 统一管理 */

static const OrpheusValue* find_parameter(const OrpheusConfig* config, const char* id) {
    for (unsigned i=0;i<config->param_count;++i)
        if (config->param_ids[i] && strcmp(config->param_ids[i],id)==0) return &config->param_values[i];
    return NULL;
}

static int fade_prepare_explicit(FadeState* s,const OrpheusConfig* config) {
    s->freeze=false;
    for (unsigned i=0;i<config->param_count;++i) {
        for (unsigned j=0;j<sizeof(fade_params)/sizeof(fade_params[0]);++j) {
            if (!config->param_ids[i] || strcmp(config->param_ids[i],fade_params[j].id)!=0) continue;
            const OrpheusValue* v=&config->param_values[i];
            if (v->type!=fade_params[j].type || (v->type==ORPHEUS_VALUE_STRING && !v->value.str))
                return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    const OrpheusValue* value=find_parameter(config,"channels");
    int32_t channels=value ? value->value.i32 : (int32_t)config->channels;
    if (channels<2 || channels>MAX_CHANNELS) return ORPHEUS_ERR_INVALID_ARG;
    s->channels=(unsigned)channels;
    value=find_parameter(config,"freeze");
    if (value) s->freeze=value->value.b;
    const char* keys[]={"chan_map","current_gains","target_gains","coefficients","remaining_blocks"};
    void* arrays[]={s->chan_map,s->current_gains,s->target_gains,s->coefficients,s->remaining_blocks};
    for (unsigned i=0;i<5;++i) {
        value=find_parameter(config,keys[i]);
        if (orpheus_parse_gain_array(value ? value->value.str : "",arrays[i],i==0 ? s->channels : 4,
                                     i==0 || i==4,i==0 ? -1 : INT32_MIN,i==0 ? 1 : INT32_MAX)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
    }
    return ORPHEUS_OK;
}

static int fade_prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    FadeState* s = (FadeState*)state;
    s->execution_mode=0;
    const OrpheusValue* mode=find_parameter(config,"execution_mode");
    if (mode) {
        if (mode->type!=ORPHEUS_VALUE_INT || mode->value.i32<0 || mode->value.i32>1)
            return ORPHEUS_ERR_INVALID_ARG;
        s->execution_mode=mode->value.i32;
    }
    if (s->execution_mode==1) return fade_prepare_explicit(s,config);
    s->channels = config->channels > 0 ? config->channels : 4;
    float fc = read_float(config, "crossover_hz", 400.0f);
    s->fade = read_float(config, "fade", 0.0f);
    float fch = read_float(config, "front_channels", 2.0f);
    s->front_channels = (uint32_t)fch;
    if (s->front_channels < 1) s->front_channels = 1;
    if (s->front_channels >= s->channels) s->front_channels = s->channels - 1;
    float ramp_ms = read_float(config, "ramp_ms", 50.0f);
    if (config->sample_rate > 0 && fc > 0.0f) {
        s->a1 = expf(-2.0f * PI_F * fc / (float)config->sample_rate);
    } else {
        s->a1 = 0.0f;
    }
    fade_targets(s->fade, &s->target_front, &s->target_back);
    s->front_gain = s->target_front;
    s->back_gain = s->target_back;
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c) s->z[c] = 0.0f;
    if (ramp_ms <= 0.0f || config->sample_rate == 0) {
        s->smoothing_coeff = 1.0f;
    } else {
        s->smoothing_coeff = 1.0f - expf(-1.0f / ((ramp_ms / 1000.0f) * (float)config->sample_rate));
        if (s->smoothing_coeff > 1.0f) s->smoothing_coeff = 1.0f;
    }
    s->crossover_hz = fc;
    s->ramp_ms = ramp_ms;
    return ORPHEUS_OK;
}
static int fade_reset(void* state) {
    FadeState* s = (FadeState*)state;
    if (s->execution_mode==1) return ORPHEUS_OK;
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c) s->z[c] = 0.0f;
    return ORPHEUS_OK;
}
static int fade_process_explicit(FadeState* s,const OrpheusProcessContext* ctx) {
    if (!ctx || ctx->input_count<2 || ctx->output_count<1 || !ctx->inputs || !ctx->outputs ||
        !ctx->inputs[0] || !ctx->inputs[1] || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* dry=ctx->inputs[0];
    const OrpheusBuffer* low=ctx->inputs[1];
    OrpheusBuffer* out=ctx->outputs[0];
    const unsigned frames=ctx->frame_count,channels=s->channels;
    if (channels<2 || channels>MAX_CHANNELS || dry->channels!=channels || low->channels!=channels ||
        out->channels!=channels || dry->format!=ORPHEUS_FORMAT_F32 || low->format!=ORPHEUS_FORMAT_F32 ||
        out->format!=ORPHEUS_FORMAT_F32 || dry->frame_capacity<frames || low->frame_capacity<frames ||
        out->frame_capacity<frames || (frames && (!dry->data || !low->data || !out->data)))
        return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned c=0;c<channels;++c)
        if (s->chan_map[c]<-1 || s->chan_map[c]>1) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned r=0;r<4;++r)
        if (!isfinite(s->current_gains[r]) || s->current_gains[r]<0 ||
            !isfinite(s->target_gains[r]) || s->target_gains[r]<0 ||
            !isfinite(s->coefficients[r]) || s->coefficients[r]<0) return ORPHEUS_ERR_INVALID_ARG;
    if (!frames) { out->frame_count=0; return ORPHEUS_OK; }
    float starts[4],coefficients[4]={1,1,1,1},gains[4];
    memcpy(starts,s->current_gains,sizeof(starts));
    for (unsigned r=0;r<2;++r) if (!s->freeze) {
        if (!s->remaining_blocks[r]) {
            starts[r]=s->target_gains[r]; starts[r+2]=s->target_gains[r+2];
        } else {
            coefficients[r]=s->coefficients[r]; coefficients[r+2]=s->coefficients[r+2];
            if (s->remaining_blocks[r]>INT32_MIN) --s->remaining_blocks[r];
        }
    }
    const float* x=(const float*)dry->data;
    const float* lpf=(const float*)low->data;
    float* y=(float*)out->data;
    for (unsigned n=0;n<frames;++n) {
        for (unsigned r=0;r<4;++r)
            gains[r]=starts[r]*(coefficients[r]==1 ? 1 : powf(coefficients[r],(float)(n+1)));
        for (unsigned c=0;c<channels;++c) {
            const unsigned i=n*channels+c;
            int32_t r=s->chan_map[c];
            if (r<0) y[i]=0;
            else {
                float high=x[i]-lpf[i];
                y[i]=lpf[i]*gains[r+2]+high*gains[r];
            }
        }
    }
    memcpy(s->current_gains,gains,sizeof(gains));
    out->frame_count=frames;
    return ORPHEUS_OK;
}
static int fade_process(void* state, const OrpheusProcessContext* ctx) {
    FadeState* s = (FadeState*)state;
    if (s->execution_mode==1) return fade_process_explicit(s,ctx);
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    float b0 = 1.0f - s->a1;
    for (uint32_t n = 0; n < frames; ++n) {
        s->front_gain += s->smoothing_coeff * (s->target_front - s->front_gain);
        s->back_gain += s->smoothing_coeff * (s->target_back - s->back_gain);
        for (uint32_t c = 0; c < ch; ++c) {
            float x = in_data[n * ch + c];
            float lpf = b0 * x + s->a1 * s->z[c];
            s->z[c] = lpf;
            float g = (c < s->front_channels) ? s->front_gain : s->back_gain;
            out_data[n * ch + c] = lpf + g * (x - lpf);
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int fade_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    FadeState* s = (FadeState*)state;
    if (s->execution_mode==1) {
        if (strcmp(param_id,"freeze")!=0) return ORPHEUS_ERR_INVALID_ARG;
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        s->freeze=value->value.b;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "fade") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        s->fade = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
        if (s->fade < -1.0f) s->fade = -1.0f;
        if (s->fade > 1.0f) s->fade = 1.0f;
        fade_targets(s->fade, &s->target_front, &s->target_back);
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int fade_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    FadeState* s = (FadeState*)state;
    if (strcmp(param_id,"execution_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->execution_mode; return ORPHEUS_OK;
    }
    if (strcmp(param_id,"freeze")==0 && s->execution_mode==1) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->freeze; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "fade") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->fade; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "front_channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->front_channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int fade_register_slots(void* state, const OrpheusRegistry* reg) {
    FadeState* s = (FadeState*)state;
#define FADE_ARRAY_SLOT(member,label,type_) \
    ORPHEUS_REG_ARRAY(reg,s,member,4,ORPHEUS_SLOT_BULK,#member,label,type_, \
                     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, \
                     .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED)
    FADE_ARRAY_SLOT(current_gains,"四组当前增益",ORPHEUS_VALUE_FLOAT);
    FADE_ARRAY_SLOT(target_gains,"四组目标增益",ORPHEUS_VALUE_FLOAT);
    FADE_ARRAY_SLOT(coefficients,"四组幂次乘数",ORPHEUS_VALUE_FLOAT);
    FADE_ARRAY_SLOT(remaining_blocks,"四组剩余块数",ORPHEUS_VALUE_INT);
#undef FADE_ARRAY_SLOT
    ORPHEUS_REG_SLOT(reg,s,freeze,ORPHEUS_SLOT_SETTING,"freeze","冻结显式包络",ORPHEUS_VALUE_BOOL,
                    .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, fade, ORPHEUS_SLOT_SETTING, "fade", "前后淡入淡出",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-1.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, crossover_hz, ORPHEUS_SLOT_SETTING, "crossover_hz", "分频点",
                     ORPHEUS_VALUE_FLOAT, .min_f32=100.0f, .max_f32=2000.0f, .unit="Hz",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=2, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, front_channels, ORPHEUS_SLOT_SETTING, "front_channels", "前组通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=31,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, ramp_ms, ORPHEUS_SLOT_SETTING, "ramp_ms", "斜坡时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface fade_interface = {
    .get_descriptor = fade_get_descriptor, .create = fade_create, .destroy = fade_destroy,
    .prepare = fade_prepare, .reset = fade_reset, .process = fade_process,
    .set_parameter = fade_set_parameter, .get_parameter = fade_get_parameter, .get_state_value = NULL,
    .register_slots = fade_register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &fade_interface; }
