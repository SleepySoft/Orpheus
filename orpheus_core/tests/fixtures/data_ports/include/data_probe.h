#ifndef ORPHEUS_TEST_DATA_PROBE_H
#define ORPHEUS_TEST_DATA_PROBE_H

#include "orpheus_abi.h"

typedef struct {
    int32_t mode;
    float fallback;
    float metrics[32];
} DataProbeState;

#endif
