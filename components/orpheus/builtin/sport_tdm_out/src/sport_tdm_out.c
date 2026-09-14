#include "orpheus_sport_tdm_out.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusPort ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .is_variable = true,
      .channels_param = "channels" }
};
static const OrpheusComponentDescriptor descriptor = {
    .id = "orpheus.builtin.sport_tdm_out", .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION, .ports = ports, .port_count = 1,
    .state_size = sizeof(SportTdmOutState), .alignment = 8,
    .realtime_safe = true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    if (config && config->state_block) { *state = config->state_block; return ORPHEUS_OK; }
    *state = calloc(1, sizeof(SportTdmOutState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { (void)state; return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    SportTdmOutState* output = (SportTdmOutState*)state;
    output->channels = config->channels;
    output->sample_rate = config->sample_rate;
    output->dst = NULL;
    output->dst_capacity = 0;
    return ORPHEUS_OK;
}
static int reset(void* state) { (void)state; return ORPHEUS_OK; }
static int process(void* state, const OrpheusProcessContext* context) {
    SportTdmOutState* output = (SportTdmOutState*)state;
    const OrpheusBuffer* input = context->inputs[0];
    if (!input) return ORPHEUS_ERR_INVALID_ARG;
    if (output->dst && output->dst_capacity >= context->frame_count) {
        memcpy(output->dst, input->data,
               (size_t)context->frame_count * output->channels * sizeof(float));
    }
    return ORPHEUS_OK;
}
static int set_parameter(void* state, const char* id, const OrpheusValue* value) {
    (void)state; (void)id; (void)value; return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_parameter(void* state, const char* id, OrpheusValue* value) {
    SportTdmOutState* output = (SportTdmOutState*)state;
    if (strcmp(id, "channels") == 0) { value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)output->channels; return ORPHEUS_OK; }
    if (strcmp(id, "sample_rate") == 0) { value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)output->sample_rate; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int register_slots(void* state, const OrpheusRegistry* registry) {
    SportTdmOutState* output = (SportTdmOutState*)state;
    ORPHEUS_REG_SLOT(registry, output, channels, ORPHEUS_SLOT_SETTING, "channels", "逻辑通道数",
                     ORPHEUS_VALUE_INT, .flags = ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(registry, output, sample_rate, ORPHEUS_SLOT_SETTING, "sample_rate", "采样率",
                     ORPHEUS_VALUE_INT, .flags = ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
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
