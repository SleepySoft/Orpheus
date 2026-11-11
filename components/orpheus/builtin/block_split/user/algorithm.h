#ifndef ORPHEUS_BUILTIN_BLOCK_SPLIT_ALGORITHM_H
#define ORPHEUS_BUILTIN_BLOCK_SPLIT_ALGORITHM_H

#include "orpheus_abi.h"

typedef struct {
    float* block;
    uint32_t cursor;
    uint64_t frame_index,epoch;
    uint32_t timeline_flags;
} orpheus_builtin_block_split_AlgorithmState;

#endif
