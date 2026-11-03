#include "orpheus_gain_ramper.h"

#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ---- 辅助函数 ---- */

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT)   return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static int32_t read_int(const OrpheusConfig* config, const char* id, int32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT)   return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int32_t)config->param_values[i].value.f32;
        }
    }
    return fallback;
}

static const char* read_string(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_STRING) return config->param_values[i].value.str;
        }
    }
    return fallback;
}

static float db_to_linear(float db) { return powf(10.0f, db / 20.0f); }

/* 解析通道映射字符串 "0,1,0,1,2,3,-1" -> chanMap[] */
static void parse_chan_map(const char* str, int32_t* chanMap, uint32_t channels, uint32_t numRampers) {
    for (uint32_t i = 0; i < GR_MAX_CHANNELS; ++i) chanMap[i] = 0;
    if (str == NULL) {
        for (uint32_t i = 0; i < channels; ++i) chanMap[i] = (int32_t)(i % numRampers);
        return;
    }
    char buf[256];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* tok = strtok(buf, ",");
    uint32_t idx = 0;
    while (tok != NULL && idx < channels) {
        int v = atoi(tok);
        if (v < 0) v = -1;               /* -1 = bypass（unity gain） */
        if (v >= (int)numRampers) v = 0;  /* 越界回退到 ramper 0 */
        chanMap[idx] = v;
        idx++;
        tok = strtok(NULL, ",");
    }
    /* 未指定的通道默认映射到 ramper 0 */
    while (idx < channels) { chanMap[idx] = 0; idx++; }
}

/* 更新 ramper 目标并计算斜坡系数（dB 域恒定速率） */
static void ramper_set_target(RamperSlot* r, float targetLinear,
                               float rampMs, float sampleRate, uint32_t blockSize) {
    r->targetGain = fmaxf(targetLinear, GR_SILENT_GAIN);
    float curDb  = 20.0f * log10f(fmaxf(r->currentGain, GR_SILENT_GAIN));
    float tgtDb  = 20.0f * log10f(r->targetGain);
    float diff   = fabsf(tgtDb - curDb);
    if (diff < 0.01f || rampMs <= 0.0f || sampleRate <= 0.0f || blockSize == 0) {
        r->rampCoeff  = 0.0f;
        r->currentGain = r->targetGain;
    } else {
        float numBlocks = rampMs / 1000.0f * sampleRate / (float)blockSize;
        if (numBlocks < 1.0f) numBlocks = 1.0f;
        r->rampCoeff = logf(r->targetGain / fmaxf(r->currentGain, GR_SILENT_GAIN)) / numBlocks;
    }
}

/* ---- 描述符 ---- */

#define ENVELOPE_PARAMETER(key, label) \
    {.id=key,.name=label,.type=ORPHEUS_VALUE_STRING, \
     .default_value={.type=ORPHEUS_VALUE_STRING,.value.str=""},.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED}
static const OrpheusParameter gr_params[] = {
    { .id = "gain_db", .name = "Target Gain", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -96.0f, .max_f32 = 24.0f, .unit = "dB",
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true },
    { .id = "ramp_ms", .name = "Ramp Time", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 30.0f },
      .min_f32 = 0.0f, .max_f32 = 5000.0f, .unit = "ms",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = GR_MAX_CHANNELS,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "num_rampers", .name = "Num Rampers", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 1 },
      .min_i32 = 1, .max_i32 = GR_MAX_RAMPERS,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "chan_map", .name = "Channel Map", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    {.id="execution_mode",.name="执行模式（0块级请求，1逐样本递推，2帧内幂次）",.type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},.min_i32=0,.max_i32=2,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true},
    ENVELOPE_PARAMETER("current_gains","各组当前线性增益"),
    ENVELOPE_PARAMETER("target_gains","各组目标线性增益"),
    ENVELOPE_PARAMETER("coefficients","各组逐样本乘数"),
    ENVELOPE_PARAMETER("remaining_blocks","各组剩余块数"),
    {.id="freeze",.name="冻结显式包络",.type=ORPHEUS_VALUE_BOOL,
     .default_value={.type=ORPHEUS_VALUE_BOOL,.value.b=false},
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true}
};
#undef ENVELOPE_PARAMETER

static const OrpheusPort gr_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor gr_descriptor = {
    .id = "orpheus.builtin.gain_ramper", .version = "1.3.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = gr_ports, .port_count = 2, .params = gr_params, .param_count = sizeof(gr_params)/sizeof(gr_params[0]),
    .state_size = sizeof(GainRamperState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* gr_get_descriptor(void) { return &gr_descriptor; }

/* ---- 生命周期 ---- */

static int gr_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(GainRamperState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int gr_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int parse_explicit_array(const char* cursor,void* values,unsigned count,bool integer,int32_t min_i32,int32_t max_i32) {
    if (!cursor) return ORPHEUS_ERR_INVALID_ARG;
    if (!*cursor) {
        for (unsigned i=0;i<count;++i) {
            if (integer) ((int32_t*)values)[i]=0;
            else ((float*)values)[i]=1;
        }
        return ORPHEUS_OK;
    }
    for (unsigned i=0;i<count;++i) {
        char* end;
        errno=0;
        if (integer) {
            long long value=strtoll(cursor,&end,10);
            if (errno==ERANGE || value<min_i32 || value>max_i32) return ORPHEUS_ERR_INVALID_ARG;
            ((int32_t*)values)[i]=(int32_t)value;
        } else {
            float value=strtof(cursor,&end);
            if (!isfinite(value) || value<0) return ORPHEUS_ERR_INVALID_ARG;
            ((float*)values)[i]=value;
        }
        if (end==cursor) return ORPHEUS_ERR_INVALID_ARG;
        while (isspace((unsigned char)*end)) ++end;
        if (i+1<count ? *end!=',' : *end!='\0') return ORPHEUS_ERR_INVALID_ARG;
        cursor=i+1<count ? end+1 : end;
    }
    return ORPHEUS_OK;
}

static int gr_prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    GainRamperState* s = (GainRamperState*)state;
    s->execution_mode=0; s->freeze=false;
    for (unsigned i=0;i<config->param_count;++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i],"execution_mode")==0) {
            const OrpheusValue* value=&config->param_values[i];
            if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<0 || value->value.i32>2)
                return ORPHEUS_ERR_INVALID_ARG;
            s->execution_mode=value->value.i32;
        }
    }
    if (s->execution_mode>0) {
        for (unsigned i=0;i<config->param_count;++i) {
            for (unsigned j=0;j<sizeof(gr_params)/sizeof(gr_params[0]);++j) {
                if (!config->param_ids[i] || strcmp(config->param_ids[i],gr_params[j].id)!=0) continue;
                const OrpheusValue* v=&config->param_values[i];
                if (v->type!=gr_params[j].type || (v->type==ORPHEUS_VALUE_STRING && !v->value.str))
                    return ORPHEUS_ERR_INVALID_ARG;
            }
            if (config->param_ids[i] && strcmp(config->param_ids[i],"freeze")==0)
                s->freeze=config->param_values[i].value.b;
        }
        int32_t channels=read_int(config,"channels",config->channels>0 ? (int32_t)config->channels : 2);
        int32_t rampers=read_int(config,"num_rampers",1);
        if (channels<1 || channels>GR_MAX_CHANNELS || rampers<1 || rampers>GR_MAX_RAMPERS)
            return ORPHEUS_ERR_INVALID_ARG;
        s->channels=(unsigned)channels; s->numRampers=(unsigned)rampers;
        if (parse_explicit_array(read_string(config,"chan_map",""),s->chanMap,s->channels,true,s->execution_mode==2 ? -2 : -1,rampers-1)!=ORPHEUS_OK ||
            parse_explicit_array(read_string(config,"current_gains",""),s->current_gains,s->numRampers,false,0,0)!=ORPHEUS_OK ||
            parse_explicit_array(read_string(config,"target_gains",""),s->target_gains,s->numRampers,false,0,0)!=ORPHEUS_OK ||
            parse_explicit_array(read_string(config,"coefficients",""),s->coefficients,s->numRampers,false,0,0)!=ORPHEUS_OK ||
            parse_explicit_array(read_string(config,"remaining_blocks",""),s->remaining_blocks,s->numRampers,true,
                                 s->execution_mode==2 ? INT32_MIN : 0,INT32_MAX)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        return ORPHEUS_OK;
    }
    s->channels   = config->channels > 0 ? config->channels : 2;
    s->numRampers = (uint32_t)read_int(config, "num_rampers", 1);
    if (s->numRampers < 1) s->numRampers = 1;
    if (s->numRampers > GR_MAX_RAMPERS) s->numRampers = GR_MAX_RAMPERS;
    s->ramp_ms      = read_float(config, "ramp_ms", 30.0f);
    s->gain_db      = read_float(config, "gain_db", 0.0f);
    s->sampleRate   = config->sample_rate > 0 ? (float)config->sample_rate : 48000.0f;
    s->blockSize    = config->block_size > 0 ? config->block_size : 32;

    /* 解析通道映射 */
    const char* mapStr = read_string(config, "chan_map", "");
    parse_chan_map(mapStr, s->chanMap, s->channels, s->numRampers);

    /* 初始化所有 ramper 到目标增益 */
    float targetLin = db_to_linear(s->gain_db);
    for (uint32_t i = 0; i < s->numRampers; ++i) {
        s->rampers[i].currentGain = targetLin;
        s->rampers[i].targetGain  = targetLin;
        s->rampers[i].rampCoeff   = 0.0f;
    }
    return ORPHEUS_OK;
}

static int gr_reset(void* state) {
    GainRamperState* s = (GainRamperState*)state;
    if (s->execution_mode>0) return ORPHEUS_OK;
    float targetLin = db_to_linear(s->gain_db);
    for (uint32_t i = 0; i < s->numRampers; ++i) {
        s->rampers[i].currentGain = targetLin;
        s->rampers[i].targetGain  = targetLin;
        s->rampers[i].rampCoeff   = 0.0f;
    }
    return ORPHEUS_OK;
}

/* ---- 实时处理 ---- */

static int process_explicit(GainRamperState* s,const OrpheusProcessContext* ctx) {
    const OrpheusBuffer* input=ctx->inputs[0];
    OrpheusBuffer* output=ctx->outputs[0];
    unsigned frames=ctx->frame_count,channels=s->channels;
    if (!input || !output || !input->data || !output->data ||
        input->channels!=channels || output->channels!=channels ||
        input->frame_capacity<frames || output->frame_capacity<frames) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned r=0;r<s->numRampers;++r)
        if (!isfinite(s->current_gains[r]) || s->current_gains[r]<0 ||
            !isfinite(s->target_gains[r]) || s->target_gains[r]<0 ||
            !isfinite(s->coefficients[r]) || s->coefficients[r]<0 ||
            (s->execution_mode==1 && s->remaining_blocks[r]<0))
            return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned c=0;c<channels;++c)
        if (s->chanMap[c]<(s->execution_mode==2 ? -2 : -1) || s->chanMap[c]>=(int32_t)s->numRampers)
            return ORPHEUS_ERR_INVALID_ARG;
    if (!frames) { output->frame_count=0; return ORPHEUS_OK; }
    float gains[GR_MAX_RAMPERS],starts[GR_MAX_RAMPERS],coefficients[GR_MAX_RAMPERS];
    for (unsigned r=0;r<s->numRampers;++r) {
        gains[r]=s->current_gains[r]; coefficients[r]=1;
        if (!s->freeze) {
            if (!s->remaining_blocks[r]) gains[r]=s->target_gains[r];
            else {
                coefficients[r]=s->coefficients[r];
                if (s->remaining_blocks[r]>INT32_MIN) --s->remaining_blocks[r];
            }
        }
        starts[r]=gains[r];
    }
    const float* source=(const float*)input->data;
    float* destination=(float*)output->data;
    for (unsigned n=0;n<frames;++n) {
        for (unsigned r=0;r<s->numRampers;++r) {
            if (s->execution_mode==2)
                gains[r]=starts[r]*(coefficients[r]==1 ? 1 : powf(coefficients[r],(float)(n+1)));
            else gains[r]*=coefficients[r];
        }
        for (unsigned c=0;c<channels;++c) {
            int32_t r=s->chanMap[c];
            if (r==-2) destination[n*channels+c]=0;
            else if (r<0) {
                if (destination!=source) memcpy(&destination[n*channels+c],&source[n*channels+c],sizeof(float));
            }
            else destination[n*channels+c]=source[n*channels+c]*gains[r];
        }
    }
    memcpy(s->current_gains,gains,s->numRampers*sizeof(float));
    output->frame_count=frames; return ORPHEUS_OK;
}

static int gr_process(void* state, const OrpheusProcessContext* ctx) {
    if (!state || !ctx || ctx->input_count<1 || ctx->output_count<1 || !ctx->inputs || !ctx->outputs)
        return ORPHEUS_ERR_INVALID_ARG;
    GainRamperState* s = (GainRamperState*)state;
    if (s->execution_mode>0) return process_explicit(s,ctx);
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;

    /* 每块更新一次 ramper（与 Rgainx 一致：per-frame 而非 per-sample） */
    for (uint32_t r = 0; r < s->numRampers; ++r) {
        RamperSlot* rp = &s->rampers[r];
        if (rp->rampCoeff != 0.0f) {
            rp->currentGain *= expf(rp->rampCoeff);
            /* 到达或越过目标则吸附 */
            if (rp->rampCoeff > 0.0f && rp->currentGain >= rp->targetGain) {
                rp->currentGain = rp->targetGain;
                rp->rampCoeff = 0.0f;
            } else if (rp->rampCoeff < 0.0f && rp->currentGain <= rp->targetGain) {
                rp->currentGain = rp->targetGain;
                rp->rampCoeff = 0.0f;
            }
        }
        if (rp->currentGain < GR_SILENT_GAIN) rp->currentGain = GR_SILENT_GAIN;
    }

    /* 应用增益：每通道按映射的 ramper currentGain 相乘 */
    for (uint32_t n = 0; n < frames; ++n) {
        for (uint32_t c = 0; c < ch; ++c) {
            int32_t ri = s->chanMap[c];
            float g = (ri >= 0 && ri < (int32_t)s->numRampers) ? s->rampers[ri].currentGain : 1.0f;
            out_data[n * ch + c] = in_data[n * ch + c] * g;
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

/* ---- 参数 ---- */

static int gr_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    if (!state || !param_id || !value) return ORPHEUS_ERR_INVALID_ARG;
    GainRamperState* s = (GainRamperState*)state;
    if (strcmp(param_id,"freeze")==0) {
        if (s->execution_mode==0) return ORPHEUS_ERR_UNSUPPORTED;
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        s->freeze=value->value.b; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "gain_db") == 0) {
        if (s->execution_mode>0) return ORPHEUS_ERR_UNSUPPORTED;
        if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT)
            return ORPHEUS_ERR_INVALID_ARG;
        s->gain_db = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
        float targetLin = db_to_linear(s->gain_db);
        for (uint32_t i = 0; i < s->numRampers; ++i) {
            ramper_set_target(&s->rampers[i], targetLin, s->ramp_ms, s->sampleRate, s->blockSize);
        }
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int gr_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    if (!state || !param_id || !value) return ORPHEUS_ERR_INVALID_ARG;
    GainRamperState* s = (GainRamperState*)state;
    if (strcmp(param_id,"execution_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->execution_mode; return ORPHEUS_OK;
    }
    if (strcmp(param_id,"freeze")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->freeze; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "gain_db") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->gain_db; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "num_rampers") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->numRampers; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

/* ---- 槽注册 ---- */

static int gr_register_slots(void* state, const OrpheusRegistry* reg) {
    if (!state || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    GainRamperState* s = (GainRamperState*)state;
    ORPHEUS_REG_SLOT(reg, s, gain_db, ORPHEUS_SLOT_SETTING, "gain_db", "目标增益",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-96.0f, .max_f32=24.0f, .unit="dB",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            (s->execution_mode==0 ? ORPHEUS_SLOT_DIRECT_WRITE : 0));
    ORPHEUS_REG_SLOT(reg, s, ramp_ms, ORPHEUS_SLOT_SETTING, "ramp_ms", "斜坡时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=5000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, numRampers, ORPHEUS_SLOT_SETTING, "num_rampers", "ramper 数量",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=GR_MAX_RAMPERS,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,execution_mode,ORPHEUS_SLOT_SETTING,"execution_mode","执行模式",ORPHEUS_VALUE_INT,
                    .min_i32=0,.max_i32=2,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,freeze,ORPHEUS_SLOT_SETTING,"freeze","冻结显式包络",ORPHEUS_VALUE_BOOL,
                    .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
#define REGISTER_ENVELOPE(field,label,type_) \
    ORPHEUS_REG_ARRAY(reg,s,field,s->numRampers,ORPHEUS_SLOT_BULK,#field,label,type_, \
                     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.flags=ORPHEUS_SLOT_DOUBLE_BUFFERED)
    REGISTER_ENVELOPE(current_gains,"各组当前线性增益",ORPHEUS_VALUE_FLOAT);
    REGISTER_ENVELOPE(target_gains,"各组目标线性增益",ORPHEUS_VALUE_FLOAT);
    REGISTER_ENVELOPE(coefficients,"各组逐样本乘数",ORPHEUS_VALUE_FLOAT);
    REGISTER_ENVELOPE(remaining_blocks,"各组剩余块数",ORPHEUS_VALUE_INT);
#undef REGISTER_ENVELOPE
    return ORPHEUS_OK;
}

/* ---- 接口 ---- */

static const OrpheusComponentInterface gr_interface = {
    .get_descriptor = gr_get_descriptor, .create = gr_create, .destroy = gr_destroy,
    .prepare = gr_prepare, .reset = gr_reset, .process = gr_process,
    .set_parameter = gr_set_parameter, .get_parameter = gr_get_parameter,
    .get_state_value = NULL, .register_slots = gr_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &gr_interface; }
