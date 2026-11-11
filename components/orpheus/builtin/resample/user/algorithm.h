#ifndef ORPHEUS_BUILTIN_RESAMPLE_ALGORITHM_H
#define ORPHEUS_BUILTIN_RESAMPLE_ALGORITHM_H

#include "orpheus_abi.h"

typedef struct {
    float* block;
    float acc[32];
    uint32_t pending;
    uint64_t frame_index,epoch;
    uint32_t timeline_flags;
} orpheus_builtin_resample_AlgorithmState;

#endif
