#ifndef ORPHEUS_BLOCK_SPLIT_H
#define ORPHEUS_BLOCK_SPLIT_H

#include "orpheus_abi.h"

typedef struct {
    uint32_t channels;
    uint32_t factor;
    uint32_t input_frames;
    uint32_t output_frames;
    uint32_t calls;
    uint32_t cursor;
    bool started;
    float* block;
} BlockSplitState;

#endif
