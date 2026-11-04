#ifndef ORPHEUS_BRIDGE_BULK_TEST_H
#define ORPHEUS_BRIDGE_BULK_TEST_H
#include "orpheus_abi.h"

typedef struct {
    int32_t integers[2048];
    float floats[2048];
    int32_t small[17];
    float snapshot[2048];
    uint32_t tick;
} BridgeBulkTestState;
#endif
