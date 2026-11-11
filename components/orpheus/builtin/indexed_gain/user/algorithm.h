#ifndef ORPHEUS_INDEXED_GAIN_ALGORITHM_H
#define ORPHEUS_INDEXED_GAIN_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float table_idx[32],table_db[32];
    uint32_t table_size,block_size,remaining;
    float sample_rate,target_gain,ramp_coeff;
} orpheus_builtin_indexed_gain_AlgorithmState;
#endif
