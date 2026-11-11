#ifndef ORPHEUS_BUILTIN_DELAY_LINE_ALGORITHM_H
#define ORPHEUS_BUILTIN_DELAY_LINE_ALGORITHM_H
#include <stdint.h>
typedef struct orpheus_builtin_delay_line_AlgorithmState {
    float* buffer;
    float delays_samples[64];
    uint32_t capacity;
    uint32_t write_pos;
} orpheus_builtin_delay_line_AlgorithmState;
#endif
