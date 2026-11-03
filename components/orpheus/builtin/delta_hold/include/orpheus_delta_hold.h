#ifndef ORPHEUS_DELTA_HOLD_H
#define ORPHEUS_DELTA_HOLD_H
#include "orpheus_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t channels,batches,hold_ticks;
    float threshold,previous[64];
    uint32_t counter[64];
    int32_t active[64];
    bool owns_memory;
} DeltaHoldState;
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void);
#ifdef __cplusplus
}
#endif
#endif
