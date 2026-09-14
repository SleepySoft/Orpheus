#include "orpheus_sport_tdm_in.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusPort ports[] = {
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .is_variable = true,
      .channels_param = "channels" }
};

static const OrpheusComponentDescriptor descriptor = {
    .id = "orpheus.builtin.sport_tdm_in", .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION, .ports = ports, .port_count = 1,
    .state_size = sizeof(SportTdmInState), .alignment = 8,
    .realtime_safe = true
};

static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    if (config && config->state_block) { *state = config->state_block; return ORPHEUS_OK; }
    *state = calloc(1, sizeof(SportTdmInState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { (void)state; return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    SportTdmInState* input = (SportTdmInState*)state;
    input->channels = config->channels;
    input->sample_rate = config->sample_rate;
    input->src = NULL;
    input->src_frames = 0;
    input->underruns = 0;
    return ORPHEUS_OK;
}
static int reset(void* state) {
    ((SportTdmInState*)state)->src_frames = 0;
    return ORPHEUS_OK;
}
static int process(void* state, const OrpheusProcessContext* context) {
    SportTdmInState* input = (SportTdmInState*)state;
    OrpheusBuffer* output = context->outputs[0];
    uint32_t frames = context->frame_count;
    uint32_t copy_frames = input->src_frames < frames ? input->src_frames : frames;
    if (!output) return ORPHEUS_ERR_INVALID_ARG;
    if (input->src && copy_frames) {
        memcpy(output->data, input->src,
               (size_t)copy_frames * input->channels * sizeof(float));
    }
    if (copy_frames < frames) {
        memset((float*)output->data + (size_t)copy_frames * input->channels, 0,
               (size_t)(frames - copy_frames) * input->channels * sizeof(float));
        input->underruns++;
    }
    output->frame_count = frames;
    output->interleaved = true;
    return ORPHEUS_OK;
}
static int set_parameter(void* state, const char* id, const OrpheusValue* value) {
    (void)state; (void)id; (void)value; return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_parameter(void* state, const char* id, OrpheusValue* value) {
    SportTdmInState* input = (SportTdmInState*)state;
    if (strcmp(id, "channels") == 0) { value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)input->channels; return ORPHEUS_OK; }
    if (strcmp(id, "sample_rate") == 0) { value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)input->sample_rate; return ORPHEUS_OK; }
    if (strcmp(id, "underruns") == 0) { value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)input->underruns; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int register_slots(void* state, const OrpheusRegistry* registry) {
    SportTdmInState* input = (SportTdmInState*)state;
    ORPHEUS_REG_SLOT(registry, input, channels, ORPHEUS_SLOT_SETTING, "channels", "逻辑通道数",
                     ORPHEUS_VALUE_INT, .flags = ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(registry, input, sample_rate, ORPHEUS_SLOT_SETTING, "sample_rate", "采样率",
                     ORPHEUS_VALUE_INT, .flags = ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(registry, input, underruns, ORPHEUS_SLOT_PROBE, "underruns", "欠载次数",
                     ORPHEUS_VALUE_INT, .flags = ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface interface = {
    get_descriptor, create, destroy, prepare, reset, process,
    set_parameter, get_parameter, NULL, register_slots, NULL
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
