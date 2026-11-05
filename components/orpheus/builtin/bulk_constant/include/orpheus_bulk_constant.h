#ifndef ORPHEUS_BULK_CONSTANT_H
#define ORPHEUS_BULK_CONSTANT_H
#include "orpheus_abi.h"
typedef struct {
    float values[2048];
    uint32_t elements;
    bool owns_memory;
} BulkConstantState;
#endif
