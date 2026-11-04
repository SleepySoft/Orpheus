#include "bridge_bulk_test.h"
#include <string.h>

static const OrpheusComponentDescriptor descriptor = {
    .id = "orpheus.test.bridge_bulk", .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .state_size = sizeof(BridgeBulkTestState), .alignment = 8, .realtime_safe = true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    *state = config->state_block;
    memset(*state, 0, sizeof(BridgeBulkTestState));
    return ORPHEUS_OK;
}
static int destroy(void* state) { (void)state; return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    (void)state; (void)config; return ORPHEUS_OK;
}
static int process(void* state, const OrpheusProcessContext* context) {
    BridgeBulkTestState* data = (BridgeBulkTestState*)state;
    data->tick = (data->tick + 1u) % 1000000u;
    for (size_t i = 0; i < 2048; ++i) data->snapshot[i] = (float)data->tick;
    return orpheus_process_next(context);
}
static int register_slots(void* state, const OrpheusRegistry* registry) {
    BridgeBulkTestState* data = (BridgeBulkTestState*)state;
    ORPHEUS_REG_ARRAY(registry, data, integers, 2048, ORPHEUS_SLOT_BULK,
        "integers", "整数数组", ORPHEUS_VALUE_INT, .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED);
    ORPHEUS_REG_ARRAY(registry, data, floats, 2048, ORPHEUS_SLOT_BULK,
        "floats", "浮点数组", ORPHEUS_VALUE_FLOAT, .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED);
    ORPHEUS_REG_ARRAY(registry, data, small, 17, ORPHEUS_SLOT_BULK,
        "small", "实际长度17", ORPHEUS_VALUE_INT, .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED);
    ORPHEUS_REG_ARRAY(registry, data, snapshot, 2048, ORPHEUS_SLOT_PROBE,
        "snapshot", "持续变化的快照源", ORPHEUS_VALUE_FLOAT);
    ORPHEUS_REG_ARRAY(registry, data, snapshot, 1, ORPHEUS_SLOT_PROBE,
        "scalar", "标量探针", ORPHEUS_VALUE_FLOAT);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface interface = {
    .get_descriptor = get_descriptor, .create = create, .destroy = destroy,
    .prepare = prepare, .process = process, .register_slots = register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
