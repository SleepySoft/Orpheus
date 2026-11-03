#ifndef ORPHEUS_BULK_ONE_POLE_H
#define ORPHEUS_BULK_ONE_POLE_H
#include "orpheus_abi.h"
typedef struct {
    uint32_t channels,elements;
    float alpha;
    float* history;
    bool absolute_input,owns_memory;
} BulkOnePoleState;
#endif
