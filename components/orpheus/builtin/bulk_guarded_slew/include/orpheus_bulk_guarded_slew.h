#ifndef ORPHEUS_BULK_GUARDED_SLEW_H
#define ORPHEUS_BULK_GUARDED_SLEW_H
#include "orpheus_abi.h"
typedef struct {
    float value,delta,change;
} GuardedSlewHistory;
typedef struct {
    uint32_t channels,elements;
    float attack,decay,threshold,slow,initial;
    GuardedSlewHistory* history;
    bool initialized,owns_memory;
} BulkGuardedSlewState;
#endif
