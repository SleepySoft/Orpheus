#ifndef ORPHEUS_INTERP_LUT_ALGORITHM_H
#define ORPHEUS_INTERP_LUT_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float axis[256],table[256],hist[64];
    uint32_t count,hist_pos,hist_count;
} orpheus_builtin_interp_lut_AlgorithmState;
#endif
