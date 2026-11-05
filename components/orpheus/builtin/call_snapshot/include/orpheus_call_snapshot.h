#ifndef ORPHEUS_CALL_SNAPSHOT_H
#define ORPHEUS_CALL_SNAPSHOT_H
#include "orpheus_abi.h"
typedef struct {
    bool owns_memory;
    float targets[40], initial[40], coefficient;
    int32_t sequence;
} CallSnapshotState;
#endif
