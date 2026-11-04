#ifndef ORPHEUS_TASK_SOURCE_H
#define ORPHEUS_TASK_SOURCE_H

#include "orpheus_abi.h"

typedef struct {
    int32_t frequency_hz;
    bool owns_memory;
} TaskSourceState;

#endif
