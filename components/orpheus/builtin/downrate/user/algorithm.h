#ifndef ORPHEUS_BUILTIN_DOWNRATE_ALGORITHM_H
#define ORPHEUS_BUILTIN_DOWNRATE_ALGORITHM_H

#include "orpheus_abi.h"

typedef struct {
    float* block;
    uint64_t frame_index,epoch;
    uint32_t timeline_flags;
} orpheus_builtin_downrate_AlgorithmState;

#endif
