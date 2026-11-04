#include "orpheus_level_tracker.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PARAMETERS(X) \
    X(rms_db,"无输入时RMS电平",-100.0f) \
    X(peak_db,"无输入时峰值电平",-100.0f) \
    X(prediction_db_spl,"声压级预测偏置",100.0f) \
    X(slow_decay_db,"慢衰减每次增量",-0.01f) \
    X(hold_margin_db,"保持触发差值（非负）",3.0f) \
    X(hold_calls,"保持调用数（非负）",10.0f) \
    X(fast_decay_db,"快衰减每次增量",-1.0f) \
    X(min_db,"包络下限",-100.0f) \
    X(max_db,"包络上限",0.0f) \
    X(volume_db,"无输入时音量补偿",0.0f) \
    X(trigger,"无输入时快速触发",0.0f) \
    X(trigger_threshold,"快速触发阈值",0.0f) \
    X(fast_multiplier,"快速触发衰减倍数",1.0f) \
    X(mode,"模式（负数冻结，0均方，正数峰值）",0.0f) \
    X(volume_epsilon,"线性音量转dB正偏置",2.22044605e-16f)

static float* parameter(LevelTrackerState* state,const char* key) {
#define FIND(member,name,value) if (!strcmp(key,#member)) return &state->member;
    PARAMETERS(FIND)
#undef FIND
    return NULL;
}

static int validate(const LevelTrackerState* state) {
#define FINITE(member,name,value) if (!isfinite(state->member)) return ORPHEUS_ERR_INVALID_ARG;
    PARAMETERS(FINITE)
#undef FINITE
    if (state->min_db>state->max_db || state->hold_calls<0 || state->hold_margin_db<0 || state->volume_epsilon<=0)
        return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}

static const OrpheusParameter params[] = {
#define DESCRIPTOR(member,label,initial) \
    {.id=#member,.name=label,.type=ORPHEUS_VALUE_FLOAT, \
     .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=initial}, \
     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,.readback=true,.persistent=true},
    PARAMETERS(DESCRIPTOR)
#undef DESCRIPTOR
    {.id="level_db_spl",.name="声压级输出",.type=ORPHEUS_VALUE_FLOAT,.readback=true},
    {.id="envelope_db",.name="当前包络",.type=ORPHEUS_VALUE_FLOAT,.readback=true},
    {.id="holding",.name="保持状态",.type=ORPHEUS_VALUE_BOOL,.readback=true}
};

static const OrpheusPort ports[] = {
#define PORT(key,dir) {.id=key,.direction=dir,.type=ORPHEUS_PORT_CONTROL, \
                      .sample_format=ORPHEUS_FORMAT_F32,.channels=1,.block_size=1}
    PORT("rms",ORPHEUS_PORT_INPUT), PORT("peak",ORPHEUS_PORT_INPUT),
    PORT("volume",ORPHEUS_PORT_INPUT), PORT("trigger",ORPHEUS_PORT_INPUT),
    PORT("volume_gain",ORPHEUS_PORT_INPUT),
    PORT("level",ORPHEUS_PORT_OUTPUT), PORT("envelope",ORPHEUS_PORT_OUTPUT)
#undef PORT
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.level_tracker",.version="1.1.0",.abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports,.port_count=7,.params=params,.param_count=sizeof(params)/sizeof(params[0]),
    .state_size=sizeof(LevelTrackerState),.alignment=8,.realtime_safe=true
};

static int tracker_create(void** state,const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    if (config && config->state_block) {
        *state=config->state_block;
        memset(*state,0,sizeof(LevelTrackerState));
        return ORPHEUS_OK;
    }
    *state=calloc(1,sizeof(LevelTrackerState));
    if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
    ((LevelTrackerState*)*state)->owns_memory=true;
    return ORPHEUS_OK;
}
static int tracker_destroy(void* state) {
    if (state && ((LevelTrackerState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}
static int tracker_reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    LevelTrackerState* s=(LevelTrackerState*)state;
    s->initialized=false; s->holding=true; s->hold_count=0; s->envelope_db=0; s->level_db_spl=0;
    return ORPHEUS_OK;
}
static int tracker_prepare(void* state,const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    LevelTrackerState candidate={0};
    candidate.owns_memory=((LevelTrackerState*)state)->owns_memory;
#define DEFAULT(member,name,value) candidate.member=value;
    PARAMETERS(DEFAULT)
#undef DEFAULT
    for (unsigned i=0;i<config->param_count;++i) {
        if (!config->param_ids[i]) return ORPHEUS_ERR_INVALID_ARG;
        float* slot=parameter(&candidate,config->param_ids[i]);
        if (!slot) return ORPHEUS_ERR_INVALID_ARG;
        if (config->param_values[i].type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        *slot=config->param_values[i].value.f32;
    }
    if (validate(&candidate)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    *(LevelTrackerState*)state=candidate;
    return tracker_reset(state);
}
static int scalar_valid(const OrpheusBuffer* buffer,unsigned frames) {
    return !buffer || (buffer->format==ORPHEUS_FORMAT_F32 && buffer->channels==1 &&
                      buffer->frame_capacity>=1 && (!frames || buffer->data));
}
static int tracker_process(void* state,const OrpheusProcessContext* context) {
    if (!state || !context || (context->input_count && !context->inputs) ||
        (context->output_count && !context->outputs)) return ORPHEUS_ERR_INVALID_ARG;
    LevelTrackerState* s=(LevelTrackerState*)state;
    for (unsigned i=0;i<context->input_count && i<5;++i)
        if (!scalar_valid(context->inputs[i],context->frame_count)) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned i=0;i<context->output_count && i<2;++i)
        if (!scalar_valid(context->outputs[i],context->frame_count)) return ORPHEUS_ERR_INVALID_ARG;
    if (context->input_count>4 && context->inputs[4] && context->inputs[2])
        return ORPHEUS_ERR_INVALID_ARG;
    if (!context->frame_count) return orpheus_process_next(context);
    float values[]={s->rms_db,s->peak_db,s->volume_db,s->trigger};
    for (unsigned i=0;i<context->input_count && i<4;++i)
        if (context->inputs[i]) values[i]=((const float*)context->inputs[i]->data)[0];
    if (context->input_count>4 && context->inputs[4])
        values[2]=log10f(((const float*)context->inputs[4]->data)[0]+s->volume_epsilon)*20;
    if (!s->initialized) { s->initialized=true; s->envelope_db=-fabsf(s->min_db); }
    s->level_db_spl=0;
    if (s->mode>=0) {
        float input=s->mode>0 ? values[1] : values[0];
        float delta=s->envelope_db-input;
        if (delta<=0) {
            s->envelope_db=input; s->hold_count=0; s->holding=true;
        } else {
            float coefficient=s->slow_decay_db;
            if (delta>=s->hold_margin_db) {
                s->hold_count++;
                if (s->hold_count>s->hold_calls) s->holding=false;
            } else s->hold_count=0;
            if (!s->holding) coefficient=s->fast_decay_db;
            if (values[3]>s->trigger_threshold) coefficient=s->fast_multiplier*s->fast_decay_db;
            s->envelope_db+=coefficient;
        }
        s->envelope_db=fmaxf(s->min_db,s->envelope_db);
        s->envelope_db=fminf(s->max_db,s->envelope_db);
        s->level_db_spl=fmaxf(0,(s->envelope_db+s->prediction_db_spl)+values[2]);
    }
    for (unsigned i=0;i<context->output_count && i<2;++i) if (context->outputs[i]) {
        OrpheusBuffer* output=context->outputs[i];
        ((float*)output->data)[0]=i ? s->envelope_db : s->level_db_spl;
        output->frame_count=1;
    }
    return orpheus_process_next(context);
}
static int tracker_set(void* state,const char* key,const OrpheusValue* value) {
    if (!state || !key || !value || value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
    LevelTrackerState candidate=*(LevelTrackerState*)state;
    float* slot=parameter(&candidate,key);
    if (!slot) return ORPHEUS_ERR_INVALID_ARG;
    *slot=value->value.f32;
    if (validate(&candidate)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    *(LevelTrackerState*)state=candidate;
    return ORPHEUS_OK;
}
static int tracker_get(void* state,const char* key,OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    LevelTrackerState* s=(LevelTrackerState*)state;
    if (!strcmp(key,"holding")) { value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->holding; return ORPHEUS_OK; }
    float* slot=parameter(s,key);
    if (!strcmp(key,"level_db_spl")) slot=&s->level_db_spl;
    if (!strcmp(key,"envelope_db")) slot=&s->envelope_db;
    if (!slot) return ORPHEUS_ERR_INVALID_ARG;
    value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=*slot;
    return ORPHEUS_OK;
}
static int tracker_register(void* state,const OrpheusRegistry* registry) {
    LevelTrackerState* s=(LevelTrackerState*)state;
#define REGISTER(member,label,value) \
    ORPHEUS_REG_SLOT(registry,s,member,ORPHEUS_SLOT_SETTING,#member,label,ORPHEUS_VALUE_FLOAT, \
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    PARAMETERS(REGISTER)
#undef REGISTER
    ORPHEUS_REG_SLOT(registry,s,level_db_spl,ORPHEUS_SLOT_PROBE,"level_db_spl","声压级输出",
                    ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry,s,envelope_db,ORPHEUS_SLOT_PROBE,"envelope_db","当前包络",
                    ORPHEUS_VALUE_FLOAT,.flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry,s,holding,ORPHEUS_SLOT_PROBE,"holding","保持状态",
                    ORPHEUS_VALUE_BOOL,.flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}
static const OrpheusComponentDescriptor* tracker_descriptor(void) { return &descriptor; }
static const OrpheusComponentInterface interface={
    .get_descriptor=tracker_descriptor,.create=tracker_create,.destroy=tracker_destroy,.prepare=tracker_prepare,
    .reset=tracker_reset,.process=tracker_process,.set_parameter=tracker_set,
    .get_parameter=tracker_get,.register_slots=tracker_register
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &interface;
}
