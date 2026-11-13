#ifndef ORPHEUS_BUILTIN_PLACEHOLDER_ALGORITHM_H
#define ORPHEUS_BUILTIN_PLACEHOLDER_ALGORITHM_H

#include <stdint.h>

typedef struct {
    int32_t routes[32];
    uint32_t route_count;
    int silence;
} orpheus_builtin_placeholder_AlgorithmState;

#endif
