#ifndef ORPHEUS_POOL_DELAY_ALGORITHM_H
#define ORPHEUS_POOL_DELAY_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float* buffer;
    uint32_t cursor;
} orpheus_builtin_pool_delay_AlgorithmState;
#endif
