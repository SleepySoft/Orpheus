#include "orpheus_indexed_tone.h"

#include <math.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define UNITY_EPSILON 4.7683716e-7f

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static int32_t read_int(const OrpheusConfig* config, const char* id, int32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int32_t)config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_BOOL) return config->param_values[i].value.b ? 1 : 0;
        }
    }
    return fallback;
}

static const char* read_string(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_STRING) {
            return config->param_values[i].value.str ? config->param_values[i].value.str : fallback;
        }
    }
    return fallback;
}

static void parse_coefs(const char* text, float out[INDEXED_TONE_COEFS]) {
    const char* cursor = text;
    for (uint32_t i = 0; i < INDEXED_TONE_COEFS; ++i) out[i] = i == 0 ? 1.0f : 0.0f;
    for (uint32_t i = 0; cursor && *cursor && i < INDEXED_TONE_COEFS; ++i) {
        char* end = NULL;
        out[i] = strtof(cursor, &end);
        if (end == cursor) break;
        cursor = end;
        while (*cursor == ',' || *cursor == ' ' || *cursor == '\t') ++cursor;
    }
}

static float index_to_gain(const IndexedToneState* state, float index) {
    float scale = fabsf(state->scale) > 1.0e-12f ? state->scale : 1.0f;
    float normalized = (index - state->offset) / scale;
    if (normalized > 1.0f) normalized = 1.0f;
    if (normalized < -1.0f) normalized = -1.0f;
    float db = normalized >= 0.0f
        ? normalized * state->max_boost_db
        : normalized * state->max_cut_db;
    return powf(10.0f, db / 20.0f);
}

static void configure_ramp(IndexedToneState* state, float target) {
    state->target_gain = fmaxf(target, INDEXED_TONE_SILENT_GAIN);
    float current = fmaxf(state->current_gain, INDEXED_TONE_SILENT_GAIN);
    float diff_db = fabsf(20.0f * log10f(state->target_gain / current));
    if (diff_db < 0.01f || state->ramp_ms <= 0.0f || state->sample_rate <= 0.0f) {
        state->current_gain = state->target_gain;
        state->ramp_coeff = 1.0f;
        state->frame_count = 0;
        return;
    }
    float duration_ms = state->ramp_ms;
    if (state->ramp_db_per_s > 0.0f) {
        float rate = fminf(50000.0f, fmaxf(5.0f, state->ramp_db_per_s));
        duration_ms = diff_db / rate * 1000.0f;
    }
    duration_ms = fminf(3000.0f, fmaxf(1.0f, duration_ms));
    float block_ms = 1000.0f * (float)state->block_size / state->sample_rate;
    state->frame_count = (uint32_t)ceilf(duration_ms / block_ms);
    if (state->frame_count < 1) state->frame_count = 1;
    state->ramp_coeff = expf(logf(state->target_gain / current) /
        ((float)state->block_size * (float)state->frame_count));
}

static void set_index(IndexedToneState* state, float index) {
    state->gain_index = index;
    state->final_target_gain = index_to_gain(state, index);
    int crosses_unity = (state->current_gain < 1.0f && state->final_target_gain > 1.0f) ||
                        (state->current_gain > 1.0f && state->final_target_gain < 1.0f);
    state->crossing_phase = crosses_unity ? 1 : 0;
    float target = state->final_target_gain;
    if (crosses_unity) target = state->current_gain < 1.0f ? 1.0f - UNITY_EPSILON : 1.0f + UNITY_EPSILON;
    configure_ramp(state, target);
}

#define RAMP_PARAMETERS(n) \
    {.id="ramp" #n "_current_gain", .name="阶段" #n "当前线性增益", .type=ORPHEUS_VALUE_FLOAT, \
     .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true}, \
    {.id="ramp" #n "_target_gain", .name="阶段" #n "目标线性增益", .type=ORPHEUS_VALUE_FLOAT, \
     .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true}, \
    {.id="ramp" #n "_coefficient", .name="阶段" #n "逐样本乘数", .type=ORPHEUS_VALUE_FLOAT, \
     .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true}, \
    {.id="ramp" #n "_remaining_blocks", .name="阶段" #n "剩余块数", .type=ORPHEUS_VALUE_INT, \
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0}, .min_i32=INT32_MIN,.max_i32=INT32_MAX, \
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true}
static const char* ramp_keys[2][4] = {
    {"ramp0_current_gain", "ramp0_target_gain", "ramp0_coefficient", "ramp0_remaining_blocks"},
    {"ramp1_current_gain", "ramp1_target_gain", "ramp1_coefficient", "ramp1_remaining_blocks"}
};
static const OrpheusParameter parameters[] = {
    { .id="channels", .name="Channels", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2}, .min_i32=1,.max_i32=64,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=true },
    { .id="gain_index", .name="Gain Index", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=128}, .min_f32=0,.max_f32=255,.update_policy=ORPHEUS_UPDATE_SMOOTHED,.readback=true,.persistent=true },
    { .id="offset", .name="Offset", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=128}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true },
    { .id="scale", .name="Scale", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=127}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true },
    { .id="max_boost_db", .name="Max Boost", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=20}, .unit="dB",.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true },
    { .id="max_cut_db", .name="Max Cut", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=20}, .unit="dB",.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true },
    { .id="ramp_ms", .name="Ramp Time", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=30}, .min_f32=0,.max_f32=3000,.unit="ms",.update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true },
    { .id="ramp_db_per_s", .name="Ramp Rate", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0}, .min_f32=0,.max_f32=50000,.unit="dB/s",.update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true },
    { .id="freeze", .name="Freeze", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL,.value.b=false}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true },
    { .id="filter_order", .name="Filter Order", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1}, .min_i32=1,.max_i32=2,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true },
    { .id="boost_coefs", .name="Boost Coefficients", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="1,0,0,0,0"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED },
    { .id="cut_coefs", .name="Cut Coefficients", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="1,0,0,0,0"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED },
    {.id="execution_mode", .name="执行模式（0索引请求，1显式两阶段）", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0}, .min_i32=0,.max_i32=1,
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true},
    {.id="phase", .name="当前阶段（1或2）", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1}, .min_i32=1,.max_i32=2,
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true},
    RAMP_PARAMETERS(0),
    RAMP_PARAMETERS(1),
};
#undef RAMP_PARAMETERS

static const OrpheusPort ports[] = {
    { .id="in",.direction=ORPHEUS_PORT_INPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels" },
    { .id="out",.direction=ORPHEUS_PORT_OUTPUT,.type=ORPHEUS_PORT_AUDIO,.sample_format=ORPHEUS_FORMAT_F32,.is_variable=true,.channels_param="channels" },
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.indexed_tone",.version="1.1.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=2,.params=parameters,.param_count=sizeof(parameters)/sizeof(parameters[0]),
    .state_size=sizeof(IndexedToneState),.scratch_size=0,.alignment=8,
    .latency_samples=0,.realtime_safe=true,.supports_inplace=false,
};

static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    if (config && config->state_block) { *state=config->state_block; return ORPHEUS_OK; }
    *state=calloc(1,sizeof(IndexedToneState)); return *state?ORPHEUS_OK:ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int parse_explicit_coefs(const char* cursor, float* values) {
    for (unsigned i=0; i<INDEXED_TONE_COEFS; ++i) {
        char* end;
        float value=strtof(cursor,&end);
        if (end==cursor || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        while (isspace((unsigned char)*end)) ++end;
        if (i+1<INDEXED_TONE_COEFS ? *end!=',' : *end!='\0') return ORPHEUS_ERR_INVALID_ARG;
        values[i]=value;
        cursor=i+1<INDEXED_TONE_COEFS ? end+1 : end;
    }
    return ORPHEUS_OK;
}

static int prepare(void* opaque, const OrpheusConfig* config) {
    if (!opaque || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    IndexedToneState* state=(IndexedToneState*)opaque;
    state->execution_mode=0;
    for (uint32_t i=0; i<config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i],"execution_mode")==0) {
            const OrpheusValue* value=&config->param_values[i];
            if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<0 || value->value.i32>1)
                return ORPHEUS_ERR_INVALID_ARG;
            state->execution_mode=value->value.i32;
        }
    }
    if (state->execution_mode==1) {
        for (uint32_t i=0; i<config->param_count; ++i) {
            for (unsigned j=0; j<sizeof(parameters)/sizeof(parameters[0]); ++j) {
                if (!config->param_ids[i] || strcmp(config->param_ids[i],parameters[j].id)!=0) continue;
                const OrpheusValue* value=&config->param_values[i];
                if (value->type!=parameters[j].type ||
                    (value->type==ORPHEUS_VALUE_FLOAT && !isfinite(value->value.f32)) ||
                    (value->type==ORPHEUS_VALUE_STRING && !value->value.str))
                    return ORPHEUS_ERR_INVALID_ARG;
            }
        }
        if (read_int(config,"channels",2)<1 || read_int(config,"channels",2)>INDEXED_TONE_MAX_CHANNELS ||
            read_int(config,"filter_order",1)<1 || read_int(config,"filter_order",1)>2)
            return ORPHEUS_ERR_INVALID_ARG;
    }
    state->channels=(uint32_t)read_int(config,"channels",2);
    if (state->channels<1) state->channels=1;
    if (state->channels>INDEXED_TONE_MAX_CHANNELS) state->channels=INDEXED_TONE_MAX_CHANNELS;
    state->offset=read_float(config,"offset",128);
    state->scale=read_float(config,"scale",127);
    state->max_boost_db=read_float(config,"max_boost_db",20);
    state->max_cut_db=read_float(config,"max_cut_db",20);
    state->ramp_ms=read_float(config,"ramp_ms",30);
    state->ramp_db_per_s=read_float(config,"ramp_db_per_s",0);
    state->freeze=read_int(config,"freeze",0)!=0;
    state->filter_order=read_int(config,"filter_order",1);
    if (state->filter_order<1) state->filter_order=1;
    if (state->filter_order>2) state->filter_order=2;
    state->sample_rate=config->sample_rate?(float)config->sample_rate:48000;
    state->block_size=config->block_size?config->block_size:32;
    parse_coefs(read_string(config,"boost_coefs","1,0,0,0,0"),state->boost_coefs);
    parse_coefs(read_string(config,"cut_coefs","1,0,0,0,0"),state->cut_coefs);
    memset(state->z1,0,sizeof(state->z1)); memset(state->z2,0,sizeof(state->z2));
    state->gain_index=read_float(config,"gain_index",128);
    state->current_gain=index_to_gain(state,state->gain_index);
    state->target_gain=state->current_gain; state->final_target_gain=state->current_gain;
    state->ramp_coeff=1; state->frame_count=0; state->crossing_phase=0;
    state->phase=read_int(config,"phase",1);
    if (state->phase<1 || state->phase>2) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned r=0; r<2; ++r) {
        IndexedToneRamp* ramp=&state->ramps[r];
        ramp->current_gain=read_float(config,ramp_keys[r][0],1);
        ramp->target_gain=read_float(config,ramp_keys[r][1],1);
        ramp->coefficient=read_float(config,ramp_keys[r][2],1);
        ramp->remaining_blocks=read_int(config,ramp_keys[r][3],0);
        if (!isfinite(ramp->current_gain) || ramp->current_gain<0 ||
            !isfinite(ramp->target_gain) || ramp->target_gain<0 ||
            !isfinite(ramp->coefficient) || ramp->coefficient<0)
            return ORPHEUS_ERR_INVALID_ARG;
    }
    if (state->execution_mode==1 &&
        (parse_explicit_coefs(read_string(config,"boost_coefs","1,0,0,0,0"),state->boost_coefs)!=ORPHEUS_OK ||
         parse_explicit_coefs(read_string(config,"cut_coefs","1,0,0,0,0"),state->cut_coefs)!=ORPHEUS_OK))
        return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}
static int reset(void* opaque) {
    IndexedToneState* state=(IndexedToneState*)opaque;
    memset(state->z1,0,sizeof(state->z1)); memset(state->z2,0,sizeof(state->z2));
    if (state->execution_mode==1) return ORPHEUS_OK;
    state->frame_count=0; state->ramp_coeff=1; state->crossing_phase=0;
    return ORPHEUS_OK;
}

static int process_explicit(IndexedToneState* state, const OrpheusProcessContext* context) {
    const OrpheusBuffer* input=context->inputs[0];
    OrpheusBuffer* output=context->outputs[0];
    uint32_t frames=context->frame_count;
    if (state->phase<1 || state->phase>2 || !input || !output || !input->data || !output->data ||
        input->channels!=state->channels || output->channels!=state->channels ||
        input->frame_capacity<frames || output->frame_capacity<frames)
        return ORPHEUS_ERR_INVALID_ARG;
    if (!frames) { output->frame_count=0; return orpheus_process_next(context); }
    unsigned phase=(unsigned)state->phase-1;
    IndexedToneRamp* ramp=&state->ramps[phase];
    float gain=ramp->current_gain, coefficient=1;
    if (!state->freeze) {
        if (ramp->remaining_blocks==0 && phase==0 && state->ramps[1].remaining_blocks>0) {
            state->phase=2; phase=1; ramp=&state->ramps[1]; gain=ramp->current_gain;
        }
        if (ramp->remaining_blocks==0) gain=ramp->target_gain;
        else {
            coefficient=ramp->coefficient;
            if (ramp->remaining_blocks>INT32_MIN) --ramp->remaining_blocks;
        }
    }
    const float* coefs=gain<1 ? state->cut_coefs : state->boost_coefs;
    const float* source=(const float*)input->data;
    float* destination=(float*)output->data;
    for (uint32_t n=0; n<frames; ++n) {
        gain*=coefficient;
        for (uint32_t c=0; c<state->channels; ++c) {
            float x=source[n*state->channels+c];
            float filtered=x*coefs[0]+state->z1[c];
            if (state->filter_order==1) state->z1[c]=x*coefs[1]+coefs[3]*filtered;
            else {
                state->z1[c]=coefs[1]==0 ? state->z2[c]-coefs[3]*filtered :
                    (x*coefs[1]+state->z2[c])-coefs[3]*filtered;
                state->z2[c]=x*coefs[2]-coefs[4]*filtered;
            }
            destination[n*state->channels+c]=(gain-1)*filtered+x;
        }
    }
    ramp->current_gain=gain;
    output->frame_count=frames;
    return orpheus_process_next(context);
}

static int process(void* opaque,const OrpheusProcessContext* context) {
    IndexedToneState* state=(IndexedToneState*)opaque;
    if (!state || !context || context->input_count<1 || context->output_count<1 ||
        !context->inputs || !context->outputs) return ORPHEUS_ERR_INVALID_ARG;
    if (state->execution_mode==1) return process_explicit(state,context);
    const OrpheusBuffer* input=context->inputs[0]; OrpheusBuffer* output=context->outputs[0];
    if(!input||!output) return ORPHEUS_ERR_INVALID_ARG;
    const float* source=(const float*)input->data; float* target=(float*)output->data;
    for(uint32_t frame=0;frame<context->frame_count;++frame){
        if(!state->freeze&&state->frame_count>0) state->current_gain*=state->ramp_coeff;
        const float* coefs=state->current_gain>=1.0f?state->boost_coefs:state->cut_coefs;
        for(uint32_t channel=0;channel<state->channels;++channel){
            float x=source[frame*state->channels+channel];
            float filtered=coefs[0]*x+state->z1[channel];
            state->z1[channel]=coefs[1]*x+state->z2[channel]-coefs[3]*filtered;
            state->z2[channel]=(state->filter_order>1?coefs[2]*x:0.0f)-coefs[4]*filtered;
            target[frame*state->channels+channel]=x+(state->current_gain-1.0f)*filtered;
        }
    }
    if(!state->freeze&&state->frame_count>0&&--state->frame_count==0){
        state->current_gain=state->target_gain; state->ramp_coeff=1;
        if(state->crossing_phase){ state->crossing_phase=0; configure_ramp(state,state->final_target_gain); }
    }
    output->frame_count=context->frame_count; return orpheus_process_next(context);
}

static int set_parameter(void* opaque,const char* id,const OrpheusValue* value){
    IndexedToneState* state=(IndexedToneState*)opaque;
    if (!state || !id || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (state->execution_mode==1 && (strcmp(id,"gain_index")==0 ||
        strcmp(id,"ramp_ms")==0 || strcmp(id,"ramp_db_per_s")==0)) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(id,"phase")==0) {
        if (value->type!=ORPHEUS_VALUE_INT || value->value.i32<1 || value->value.i32>2)
            return ORPHEUS_ERR_INVALID_ARG;
        state->phase=value->value.i32; return ORPHEUS_OK;
    }
    for (unsigned r=0; r<2; ++r) for (unsigned j=0; j<4; ++j) {
        if (strcmp(id,ramp_keys[r][j])!=0) continue;
        IndexedToneRamp* ramp=&state->ramps[r];
        if (j==3) {
            if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
            ramp->remaining_blocks=value->value.i32;
        } else {
            if (value->type!=ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32) || value->value.f32<0)
                return ORPHEUS_ERR_INVALID_ARG;
            if (j==0) ramp->current_gain=value->value.f32;
            else if (j==1) ramp->target_gain=value->value.f32;
            else ramp->coefficient=value->value.f32;
        }
        return ORPHEUS_OK;
    }
    if(strcmp(id,"gain_index")==0&&(value->type==ORPHEUS_VALUE_FLOAT||value->type==ORPHEUS_VALUE_INT)){
        set_index(state,value->type==ORPHEUS_VALUE_FLOAT?value->value.f32:(float)value->value.i32); return ORPHEUS_OK;
    }
    if(strcmp(id,"ramp_ms")==0&&(value->type==ORPHEUS_VALUE_FLOAT||value->type==ORPHEUS_VALUE_INT)){
        state->ramp_ms=value->type==ORPHEUS_VALUE_FLOAT?value->value.f32:(float)value->value.i32; return ORPHEUS_OK;
    }
    if(strcmp(id,"ramp_db_per_s")==0&&(value->type==ORPHEUS_VALUE_FLOAT||value->type==ORPHEUS_VALUE_INT)){
        state->ramp_db_per_s=value->type==ORPHEUS_VALUE_FLOAT?value->value.f32:(float)value->value.i32; return ORPHEUS_OK;
    }
    if(strcmp(id,"freeze")==0&&(value->type==ORPHEUS_VALUE_BOOL||value->type==ORPHEUS_VALUE_INT)){
        state->freeze=value->type==ORPHEUS_VALUE_BOOL?value->value.b:value->value.i32!=0; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int get_parameter(void* opaque,const char* id,OrpheusValue* value){
    IndexedToneState* state=(IndexedToneState*)opaque;
    if (!state || !id || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(id,"execution_mode")==0 || strcmp(id,"phase")==0) {
        value->type=ORPHEUS_VALUE_INT;
        value->value.i32=strcmp(id,"phase")==0 ? state->phase : state->execution_mode;
        return ORPHEUS_OK;
    }
    for (unsigned r=0; r<2; ++r) for (unsigned j=0; j<4; ++j) {
        if (strcmp(id,ramp_keys[r][j])!=0) continue;
        if (j==3) { value->type=ORPHEUS_VALUE_INT; value->value.i32=state->ramps[r].remaining_blocks; }
        else {
            value->type=ORPHEUS_VALUE_FLOAT;
            value->value.f32=j==0 ? state->ramps[r].current_gain : j==1 ? state->ramps[r].target_gain : state->ramps[r].coefficient;
        }
        return ORPHEUS_OK;
    }
    if(strcmp(id,"gain_index")==0){value->type=ORPHEUS_VALUE_FLOAT;value->value.f32=state->gain_index;return ORPHEUS_OK;}
    if(strcmp(id,"channels")==0){value->type=ORPHEUS_VALUE_INT;value->value.i32=(int32_t)state->channels;return ORPHEUS_OK;}
    if(strcmp(id,"freeze")==0){value->type=ORPHEUS_VALUE_BOOL;value->value.b=state->freeze!=0;return ORPHEUS_OK;}
    return ORPHEUS_ERR_NOT_FOUND;
}
static int register_slots(void* opaque,const OrpheusRegistry* registry){
    if (!opaque || !registry || !registry->add) return ORPHEUS_ERR_INVALID_ARG;
    IndexedToneState* state=(IndexedToneState*)opaque;
#define REGISTER_FIELD(field, index) \
    ORPHEUS_REG_SLOT(registry,state,field,ORPHEUS_SLOT_SETTING,parameters[index].id,parameters[index].name,parameters[index].type, \
        .min_f32=parameters[index].min_f32,.max_f32=parameters[index].max_f32, \
        .min_i32=parameters[index].min_i32,.max_i32=parameters[index].max_i32, \
        .update_policy=parameters[index].update_policy, \
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | (parameters[index].affects_signature ? ORPHEUS_SLOT_AFFECTS_SIGNATURE : 0))
    REGISTER_FIELD(channels,0); REGISTER_FIELD(gain_index,1);
    REGISTER_FIELD(offset,2); REGISTER_FIELD(scale,3);
    REGISTER_FIELD(max_boost_db,4); REGISTER_FIELD(max_cut_db,5);
    REGISTER_FIELD(ramp_ms,6); REGISTER_FIELD(ramp_db_per_s,7);
    REGISTER_FIELD(freeze,8); REGISTER_FIELD(filter_order,9);
    REGISTER_FIELD(execution_mode,12); REGISTER_FIELD(phase,13);
    REGISTER_FIELD(ramps[0].current_gain,14); REGISTER_FIELD(ramps[0].target_gain,15);
    REGISTER_FIELD(ramps[0].coefficient,16); REGISTER_FIELD(ramps[0].remaining_blocks,17);
    REGISTER_FIELD(ramps[1].current_gain,18); REGISTER_FIELD(ramps[1].target_gain,19);
    REGISTER_FIELD(ramps[1].coefficient,20); REGISTER_FIELD(ramps[1].remaining_blocks,21);
#undef REGISTER_FIELD
    ORPHEUS_REG_ARRAY(registry,state,boost_coefs,INDEXED_TONE_COEFS,ORPHEUS_SLOT_BULK,"boost_coefs","Boost系数",
        ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_DOUBLE_BUFFERED);
    ORPHEUS_REG_ARRAY(registry,state,cut_coefs,INDEXED_TONE_COEFS,ORPHEUS_SLOT_BULK,"cut_coefs","Cut系数",
        ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_DOUBLE_BUFFERED);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface interface={
    get_descriptor,create,destroy,prepare,reset,process,set_parameter,get_parameter,NULL,register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void){return &interface;}
