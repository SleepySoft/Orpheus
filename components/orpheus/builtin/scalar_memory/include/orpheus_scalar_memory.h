#ifndef ORPHEUS_SCALAR_MEMORY_H
#define ORPHEUS_SCALAR_MEMORY_H
#include "orpheus_abi.h"
#include "orpheus_chain.h"
typedef struct {
    float initial, value;
    bool write_after_call, initialized, owns_memory;
} ScalarMemoryState;
#endif
