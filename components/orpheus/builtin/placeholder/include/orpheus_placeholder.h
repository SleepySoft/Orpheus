#ifndef ORPHEUS_PLACEHOLDER_H
#define ORPHEUS_PLACEHOLDER_H

#include "orpheus_abi.h"

typedef struct {
    int silence;
    int routes[32];
    uint32_t route_count;
} PlaceholderState;

#endif
